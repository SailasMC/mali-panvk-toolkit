package com.dsh.gputest;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.method.ScrollingMovementMethod;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.security.MessageDigest;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * GPU 驱动测试台 v0.2
 *  ① 扫描驱动：已安装插件包（fclPlugin/driver 元数据）+ 本地 APK + 手动 .so
 *  ② 从 APK 提取驱动 .so（java.util.zip）
 *  ③ 驱动自测（原生 ICD 冒烟）
 *  ④ 三角形绘制 + 像素校验（原生）
 *  ⑤ 跑分：填充率 / 提交吞吐 / 提交往返（原生）
 *  ⑥ 拉起 Minecraft 启动器
 *  ⑦ 导出/分享日志
 */
public class MainActivity extends Activity {

    static { System.loadLibrary("gputest"); }
    public native String nativeIcdSmoke(String soPath);
    public native String nativeTri(String soPath);
    public native String nativeBench(String soPath, int seconds, String mode);

    private LinearLayout root;
    private RadioGroup driverGroup;
    private TextView logView;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final List<String> driverPaths = new ArrayList<>();
    private final StringBuilder fullLog = new StringBuilder();
    private String lastExtracted = null;
    private static final int REQ_PICK_APK = 1001;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        ScrollView sc = new ScrollView(this);
        root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int p = dp(12);
        root.setPadding(p, p, p, p);
        sc.addView(root);

        root.addView(title("GPU 驱动测试台 v0.2"));
        root.addView(note("OPPO PHZ110 · 天玑 9300 · Immortalis-G720 MC12 · 无 root\n"
                + "驱动 = Vulkan ICD（.so）：直接 dlopen，不依赖系统 Vulkan loader"));

        root.addView(btn("① 扫描驱动（插件包 + APK + .so）", v -> scanDrivers()));
        root.addView(btn("② 从 APK 提取驱动 .so（自动找最大 .so）", v -> pickApkForExtract()));
        root.addView(btn("③ 安装驱动插件 APK（选文件）", v -> pickApkForInstall()));

        root.addView(sect("驱动列表（选中用于测试）"));
        driverGroup = new RadioGroup(this);
        driverGroup.setOrientation(RadioGroup.VERTICAL);
        root.addView(driverGroup);

        root.addView(btn("④ 驱动自测（原生：建实例 / 枚举设备）", v -> runSmoke()));
        root.addView(btn("⑤ 三角形绘制 + 像素校验（原生）", v -> runTri()));
        root.addView(btn("⑥ 跑分 · 填充率（5 秒）", v -> runBench("fill")));
        root.addView(btn("⑦ 拉起 Minecraft 启动器", v -> launchChooser()));
        root.addView(btn("⑧ 导出 / 分享日志", v -> exportLog()));
        root.addView(btn("⑨ 清空日志", v -> { fullLog.setLength(0); logView.setText(""); }));

        root.addView(sect("日志"));
        logView = new TextView(this);
        logView.setTypeface(Typeface.MONOSPACE);
        logView.setTextSize(10);
        logView.setTextColor(Color.parseColor("#DDFFFFFF"));
        logView.setBackgroundColor(Color.parseColor("#FF101418"));
        logView.setPadding(p, p, p, p);
        logView.setMovementMethod(new ScrollingMovementMethod());
        logView.setTextIsSelectable(true);
        root.addView(logView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(340)));

        setContentView(sc);
        log("GPU 驱动测试台 v0.2 启动。点「① 扫描驱动」开始。");
        scanDrivers();
    }

    // ------------------------------------------------------------ ① 扫描
    private void scanDrivers() {
        driverPaths.clear(); driverGroup.removeAllViews();
        log("\n=== ① 扫描驱动 ===");
        PackageManager pm = getPackageManager();
        int found = 0;

        for (PackageInfo pi : pm.getInstalledPackages(PackageManager.GET_META_DATA)) {
            ApplicationInfo ai = pi.applicationInfo;
            if (ai == null || ai.metaData == null) continue;
            boolean isPlugin = ai.metaData.getBoolean("fclPlugin", false);
            String drvName = ai.metaData.getString("driver");
            if (!isPlugin && drvName == null) continue;
            String so = findSoIn(ai.nativeLibraryDir);
            if (so == null) { log("  ! " + pi.packageName + " 未找到 .so，跳过"); continue; }
            String label = (drvName != null ? drvName : pi.packageName) + " [" + pi.versionName + "]";
            addDriver(label, so);
            log("  ✓ 插件: " + pi.packageName + "\n      driver=" + drvName + "  version=" + pi.versionName
                    + "\n      so=" + so + "  (" + (new File(so).length() / 1048576) + " MB)");
            found++;
        }
        for (File dir : new File[]{ new File("/sdcard/Mali驱动项目/驱动"),
                                    new File("/sdcard/Mali驱动项目/.probe"),
                                    new File(getExternalFilesDir(null), "drivers"),
                                    new File("/sdcard/Download") }) {
            File[] fs = dir.listFiles();
            if (fs == null) continue;
            for (File f : fs) {
                String n = f.getName().toLowerCase(Locale.ROOT);
                if (n.endsWith(".so") && (n.contains("vulkan") || n.contains("panfrost") || n.contains("freedreno"))) {
                    addDriver("[so] " + f.getName(), f.getAbsolutePath()); found++;
                }
            }
        }
        log("  共 " + found + " 个候选驱动" + (found == 0 ? "（可先装插件 APK，或用②从 APK 提取）" : ""));
    }

    private void addDriver(String label, String path) {
        driverPaths.add(path);
        RadioButton rb = new RadioButton(this);
        rb.setText(label); rb.setTextColor(Color.WHITE);
        rb.setTag(driverPaths.size() - 1);
        driverGroup.addView(rb);
        if (driverPaths.size() == 1) rb.setChecked(true);
    }

    private String selectedPath() {
        int id = driverGroup.getCheckedRadioButtonId();
        if (id < 0) return null;
        View v = driverGroup.findViewById(id);
        return v == null ? null : driverPaths.get((Integer) v.getTag());
    }

    private String findSoIn(String dir) {
        if (dir == null) return null;
        File[] fs = new File(dir).listFiles();
        if (fs == null) return null;
        File best = null;
        for (File f : fs) {
            if (!f.getName().endsWith(".so")) continue;
            if (f.getName().contains("vulkan")) return f.getAbsolutePath();
            if (best == null || f.length() > best.length()) best = f;
        }
        return best == null ? null : best.getAbsolutePath();
    }

    // ------------------------------------------------- ② 从 APK 提取 .so
    private void pickApkForExtract() { pickFile("application/vnd.android.package-archive", REQ_PICK_APK); }
    private void pickApkForInstall() { pickFile("application/vnd.android.package-archive", REQ_PICK_APK + 1); }

    private void pickFile(String type, int req) {
        try {
            Intent i = new Intent(Intent.ACTION_GET_CONTENT);
            i.setType("*/*");
            i.addCategory(Intent.CATEGORY_OPENABLE);
            startActivityForResult(Intent.createChooser(i, "选择 APK 文件"), req);
        } catch (Throwable t) { log("  ✗ 打开文件选择器失败: " + t); }
    }

    @Override protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (res != RESULT_OK || data == null || data.getData() == null) { log("  （未选择文件）"); return; }
        Uri uri = data.getData();
        if (req == REQ_PICK_APK) extractSoFromApk(uri);
        else if (req == REQ_PICK_APK + 1) installApk(uri);
    }

    private File copyUriToCache(Uri uri, String name) {
        try {
            File dst = new File(getCacheDir(), name);
            InputStream in = getContentResolver().openInputStream(uri);
            OutputStream out = new FileOutputStream(dst);
            byte[] buf = new byte[1 << 16]; int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            in.close(); out.close();
            return dst;
        } catch (Throwable t) { log("  ✗ 读取文件失败: " + t); return null; }
    }

    private void extractSoFromApk(Uri uri) {
        log("\n=== ② 从 APK 提取驱动 .so ===");
        new Thread(() -> {
            File apk = copyUriToCache(uri, "src.apk");
            if (apk == null) return;
            log("  APK: " + apk.length() + " 字节");
            try (ZipFile z = new ZipFile(apk)) {
                ZipEntry best = null;
                java.util.Enumeration<? extends ZipEntry> e = z.entries();
                while (e.hasMoreElements()) {
                    ZipEntry en = e.nextElement();
                    String n = en.getName();
                    if (!n.startsWith("lib/") || !n.endsWith(".so")) continue;
                    if (best == null || en.getSize() > best.getSize()) best = en;
                }
                if (best == null) { log("  ✗ 该 APK 内没有 lib/**/*.so"); return; }
                File dir = new File(getExternalFilesDir(null), "drivers");
                dir.mkdirs();
                File out = new File(dir, new File(best.getName()).getName());
                InputStream in = z.getInputStream(best);
                OutputStream os = new FileOutputStream(out);
                byte[] buf = new byte[1 << 16]; int n;
                while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
                in.close(); os.close();
                lastExtracted = out.getAbsolutePath();
                log("  ✓ 提取 " + best.getName() + " (" + best.getSize() + " 字节)");
                log("    → " + lastExtracted);
                log("    sha256(16)=" + sha16(out));
                log("    已自动选中，可直接点「④ 驱动自测」或「⑤ 三角形绘制」");
                ui.post(() -> { addDriver("[提取] " + out.getName(), out.getAbsolutePath());
                                ((RadioButton) driverGroup.getChildAt(driverGroup.getChildCount() - 1)).setChecked(true); });
            } catch (Throwable t) { log("  ✗ 解压失败: " + t); }
        }).start();
    }

    private void installApk(Uri uri) {
        log("\n=== ③ 安装 APK ===");
        File apk = copyUriToCache(uri, "install.apk");
        if (apk == null) return;
        try {
            Intent i = new Intent(Intent.ACTION_VIEW);
            i.setDataAndType(Uri.fromFile(apk), "application/vnd.android.package-archive");
            i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(i);
            log("  ✓ 已调起系统安装器");
        } catch (Throwable t) {
            log("  ✗ 调起安装器失败（Android 7+ 的 file:// 限制）: " + t);
            log("    替代方案：用文件管理器打开 " + apk.getAbsolutePath());
        }
    }

    // ------------------------------------------------------- ④⑤⑥ 原生测试
    private String stageDriver(String src) {
        if (src == null) return null;
        try {
            File dst = new File(getFilesDir(), "drv.so");
            InputStream in = new FileInputStream(src);
            OutputStream out = new FileOutputStream(dst);
            byte[] buf = new byte[1 << 16]; int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            in.close(); out.close();
            dst.setReadable(true, true); dst.setExecutable(true, true);
            return dst.getAbsolutePath();
        } catch (Throwable t) { log("  ✗ 暂存驱动失败: " + t); return null; }
    }

    private void runNative(final String title, final int which, final String mode) {
        final String p = selectedPath();
        log("\n=== " + title + " ===");
        if (p == null) { log("  ✗ 还没选驱动"); toast("先选一个驱动"); return; }
        log("  驱动: " + p + "\n  sha256(16)=" + sha16(new File(p)));
        new Thread(() -> {
            String staged = stageDriver(p);
            if (staged == null) return;
            try {
                String out = (which == 0) ? nativeIcdSmoke(staged)
                           : (which == 1) ? nativeTri(staged)
                           : nativeBench(staged, 5, mode);
                for (String line : out.split("\n")) log("  " + line);
            } catch (Throwable t) { log("  ✗ 原生调用异常: " + t); }
        }).start();
    }

    private void runSmoke() { runNative("④ 驱动自测", 0, null); }
    private void runTri()   { runNative("⑤ 三角形绘制 + 像素校验", 1, null); }
    private void runBench(String mode) { runNative("⑥ 跑分 · " + mode, 2, mode); }

    // ------------------------------------------------------------ ⑦ 启动器
    private void launchChooser() {
        log("\n=== ⑦ Minecraft 启动器 ===");
        PackageManager pm = getPackageManager();
        for (PackageInfo pi : pm.getInstalledPackages(0)) {
            String n = pi.packageName.toLowerCase(Locale.ROOT);
            if (n.contains("zalith") || n.contains("fcl") || n.contains("foldcraft") || n.contains("pojav")) {
                log("  ✓ 发现: " + pi.packageName + " [" + pi.versionName + "]");
                Intent i = pm.getLaunchIntentForPackage(pi.packageName);
                if (i != null) { try { startActivity(i); log("    → 已拉起"); } catch (Throwable t) { log("    ✗ " + t); } }
                return;
            }
        }
        log("  ✗ 没找到已安装的启动器");
    }

    // ------------------------------------------------------------ ⑧ 导出
    private void exportLog() {
        try {
            File dir = getExternalFilesDir(null);
            File f = new File(dir, "gputest-" + new SimpleDateFormat("MMdd-HHmmss", Locale.ROOT).format(new Date()) + ".log");
            OutputStream os = new FileOutputStream(f);
            os.write(fullLog.toString().getBytes("UTF-8")); os.close();
            log("\n=== ⑧ 日志已导出 ===\n  " + f.getAbsolutePath());
            Intent s = new Intent(Intent.ACTION_SEND);
            s.setType("text/plain");
            s.putExtra(Intent.EXTRA_STREAM, Uri.fromFile(f));
            s.putExtra(Intent.EXTRA_TEXT, "GPU 驱动测试台日志");
            startActivity(Intent.createChooser(s, "分享日志"));
        } catch (Throwable t) { log("  ✗ 导出失败: " + t); }
    }

    // ---------------------------------------------------------------- 工具
    private String sha16(File f) {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            InputStream in = new FileInputStream(f);
            byte[] buf = new byte[1 << 16]; int n;
            while ((n = in.read(buf)) > 0) md.update(buf, 0, n);
            in.close();
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < 8; i++) sb.append(String.format("%02x", md.digest()[i]));
            return sb.toString();
        } catch (Throwable t) { return "?"; }
    }

    private int dp(int v) { return (int) (v * getResources().getDisplayMetrics().density); }
    private void toast(String s) { ui.post(() -> Toast.makeText(this, s, Toast.LENGTH_SHORT).show()); }

    private TextView title(String s) {
        TextView t = new TextView(this); t.setText(s); t.setTextSize(18);
        t.setTypeface(Typeface.DEFAULT_BOLD); t.setTextColor(Color.WHITE); return t;
    }
    private TextView sect(String s) {
        TextView t = new TextView(this); t.setText("\n" + s); t.setTextSize(14);
        t.setTypeface(Typeface.DEFAULT_BOLD); t.setTextColor(Color.parseColor("#FF7FD1FF"));
        t.setPadding(0, dp(10), 0, dp(4)); return t;
    }
    private TextView note(String s) {
        TextView t = new TextView(this); t.setText(s); t.setTextSize(11);
        t.setTextColor(Color.parseColor("#FF9AA4B2")); t.setPadding(0, 0, 0, dp(8)); return t;
    }
    private Button btn(String s, View.OnClickListener l) {
        Button b = new Button(this); b.setText(s); b.setAllCaps(false); b.setOnClickListener(l);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.setMargins(0, dp(3), 0, dp(3)); b.setLayoutParams(lp);
        b.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
        return b;
    }

    private void log(String s) {
        final String line = new SimpleDateFormat("HH:mm:ss", Locale.ROOT).format(new Date()) + "  " + s;
        fullLog.append(line).append("\n");
        ui.post(() -> { if (logView != null) logView.append(line + "\n"); });
    }
}
