// Pac-Land MD ROM Maker: Windows front end.
// A single executable: the maker's source files are embedded. On first use it
// fetches its own pinned, checksum-verified build tools into the user's
// AppData folder and never relies on anything else installed on the PC.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Forms;

[assembly: System.Reflection.AssemblyTitle("Pac-Land MD ROM Maker")]
[assembly: System.Reflection.AssemblyProduct("Pac-Land MD ROM Maker")]
[assembly: System.Reflection.AssemblyVersion("1.0.0.0")]

static class Program
{
    [STAThread]
    static void Main(string[] args)
    {
        bool fresh;
        using (var mutex = new Mutex(true, "PacLandMDRomMaker-SingleInstance", out fresh))
        {
            if (!fresh)
            {
                MessageBox.Show("Pac-Land MD ROM Maker is already open.", "Pac-Land MD ROM Maker",
                    MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm(args));
        }
    }
}

class MainForm : Form
{
    // ---- layout ----
    readonly Label dropLabel = new Label();
    readonly Button chooseButton = new Button();
    readonly TextBox romStatus = new TextBox();
    readonly Button buildButton = new Button();
    readonly ProgressBar progress = new ProgressBar();
    readonly Label stepLabel = new Label();
    readonly Button openButton = new Button();
    readonly Button detailsButton = new Button();
    readonly Label toolsLabel = new Label();
    readonly Panel dropPanel = new Panel();

    // ---- state ----
    readonly string home;          // %LOCALAPPDATA%\PacLandMD
    string root;                   // maker folder for this version
    string outputDir;              // Documents\Pac-Land MD
    string[] romPaths = new string[0];
    bool toolsReady, romOk, busy;
    string lastLog = "";
    string lastRom;

    public MainForm(string[] args)
    {
        Text = "Pac-Land MD ROM Maker";
        Font = new Font("Segoe UI", 10f);
        ClientSize = new Size(620, 470);
        FormBorderStyle = FormBorderStyle.FixedSingle;
        MaximizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;
        AllowDrop = true;
        DragEnter += OnDragEnter;
        DragDrop += OnDragDrop;

        var title = new Label { Text = "Pac-Land MD ROM Maker", Font = new Font("Segoe UI", 16f, FontStyle.Bold),
            AutoSize = true, Location = new Point(20, 15) };
        var sub = new Label { Text = "Builds the Mega Drive / Genesis cartridge from your own Pac-Land arcade ROM.",
            AutoSize = true, Location = new Point(22, 52), ForeColor = Color.DimGray };

        var step1 = new Label { Text = "1.  Your Pac-Land ROM", Font = new Font("Segoe UI", 11f, FontStyle.Bold),
            AutoSize = true, Location = new Point(20, 88) };
        dropPanel.Location = new Point(22, 115);
        dropPanel.Size = new Size(576, 70);
        dropPanel.BorderStyle = BorderStyle.FixedSingle;
        dropPanel.BackColor = Color.FromArgb(245, 247, 250);
        dropPanel.AllowDrop = true;
        dropPanel.DragEnter += OnDragEnter;
        dropPanel.DragDrop += OnDragDrop;
        dropLabel.Text = "Drag your pacland.zip here (or the folder it is in)";
        dropLabel.AutoSize = false;
        dropLabel.Size = new Size(400, 68);
        dropLabel.Location = new Point(10, 0);
        dropLabel.TextAlign = ContentAlignment.MiddleLeft;
        dropLabel.AllowDrop = true;
        dropLabel.DragEnter += OnDragEnter;
        dropLabel.DragDrop += OnDragDrop;
        chooseButton.Text = "Choose...";
        chooseButton.Size = new Size(130, 34);
        chooseButton.Location = new Point(430, 17);
        chooseButton.Click += OnChoose;
        dropPanel.Controls.Add(dropLabel);
        dropPanel.Controls.Add(chooseButton);

        romStatus.Location = new Point(22, 192);
        romStatus.Size = new Size(576, 92);
        romStatus.Multiline = true;
        romStatus.ReadOnly = true;
        romStatus.BorderStyle = BorderStyle.None;
        romStatus.BackColor = SystemColors.Control;
        romStatus.ScrollBars = ScrollBars.Vertical;
        romStatus.TabStop = false;
        romStatus.Text = "";

        var step2 = new Label { Text = "2.  Build", Font = new Font("Segoe UI", 11f, FontStyle.Bold),
            AutoSize = true, Location = new Point(20, 290) };
        buildButton.Text = "Build my ROM";
        buildButton.Font = new Font("Segoe UI", 11f, FontStyle.Bold);
        buildButton.Size = new Size(170, 42);
        buildButton.Location = new Point(22, 318);
        buildButton.Enabled = false;
        buildButton.Click += OnBuild;
        progress.Location = new Point(210, 326);
        progress.Size = new Size(388, 26);
        stepLabel.Location = new Point(22, 368);
        stepLabel.Size = new Size(576, 44);

        openButton.Text = "Open the folder with my ROM";
        openButton.Size = new Size(250, 34);
        openButton.Location = new Point(22, 414);
        openButton.Visible = false;
        openButton.Click += (s, e) => OpenOutput();
        detailsButton.Text = "Copy details for support";
        detailsButton.Size = new Size(210, 34);
        detailsButton.Location = new Point(388, 414);
        detailsButton.Visible = false;
        detailsButton.Click += (s, e) => { try { Clipboard.SetText(lastLog.Length > 0 ? lastLog : "(no details)"); } catch { } };

        toolsLabel.Location = new Point(290, 92);
        toolsLabel.Size = new Size(308, 20);
        toolsLabel.TextAlign = ContentAlignment.MiddleRight;
        toolsLabel.ForeColor = Color.DimGray;
        toolsLabel.Font = new Font("Segoe UI", 8.5f);

        Controls.AddRange(new Control[] { title, sub, step1, dropPanel, romStatus, step2, buildButton, progress,
            stepLabel, openButton, detailsButton, toolsLabel });

        home = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PacLandMD");
        outputDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "Pac-Land MD");
        Shown += (s, e) => { StartPreparation(); if (args.Length > 0) SetRoms(args); };
    }

    // ------------------------------------------------------------------ UI helpers
    void Ui(Action a) { if (IsDisposed) return; if (InvokeRequired) BeginInvoke(a); else a(); }
    void Step(string text) { Ui(() => stepLabel.Text = text); }
    void Progress(int value) { Ui(() => { progress.Style = ProgressBarStyle.Continuous; progress.Value = Math.Max(0, Math.Min(100, value)); }); }
    void Marquee() { Ui(() => progress.Style = ProgressBarStyle.Marquee); }
    void UpdateButtons() { Ui(() => buildButton.Enabled = toolsReady && romOk && !busy); }

    void OnDragEnter(object sender, DragEventArgs e)
    {
        e.Effect = e.Data.GetDataPresent(DataFormats.FileDrop) && !busy ? DragDropEffects.Copy : DragDropEffects.None;
    }

    void OnDragDrop(object sender, DragEventArgs e)
    {
        var files = e.Data.GetData(DataFormats.FileDrop) as string[];
        if (files != null && files.Length > 0 && !busy) SetRoms(files);
    }

    void OnChoose(object sender, EventArgs e)
    {
        using (var dialog = new OpenFileDialog())
        {
            dialog.Title = "Choose your Pac-Land ROM ZIP";
            dialog.Filter = "ZIP files (*.zip)|*.zip|All files (*.*)|*.*";
            dialog.Multiselect = true;
            if (dialog.ShowDialog(this) == DialogResult.OK) SetRoms(dialog.FileNames);
        }
    }

    void SetRoms(string[] paths)
    {
        romPaths = paths;
        romOk = false;
        openButton.Visible = false;
        dropLabel.Text = string.Join("\n", paths.Select(p => Path.GetFileName(p.TrimEnd(new[] { '\\' })) ?? p).Take(3).ToArray());
        UpdateButtons();
        if (toolsReady) CheckRom();
        else ShowRomStatus("Got it. Your ROM will be checked as soon as the build tools are ready.", Color.DimGray);
    }

    void ShowRomStatus(string text, Color colour) { Ui(() => { romStatus.Text = text.Replace("\r\n", "\n").Replace("\n", "\r\n"); romStatus.ForeColor = colour; }); }

    // ------------------------------------------------------------------ first-run preparation
    void StartPreparation()
    {
        busy = true;
        UpdateButtons();
        new Thread(() =>
        {
            try
            {
                Prepare();
                toolsReady = true;
                Ui(() => { toolsLabel.Text = "Build tools: ready"; Progress(0); Step(""); });
                busy = false;
                UpdateButtons();
                if (romPaths.Length > 0) CheckRom();
            }
            catch (FriendlyException ex)
            {
                busy = false;
                Ui(() =>
                {
                    toolsLabel.Text = "Build tools: not ready";
                    Progress(0);
                    Step(ex.Message);
                    stepLabel.ForeColor = Color.Firebrick;
                    if (MessageBox.Show(this, ex.Message + "\n\nTry again now?", "Pac-Land MD ROM Maker",
                        MessageBoxButtons.RetryCancel, MessageBoxIcon.Warning) == DialogResult.Retry)
                    {
                        stepLabel.ForeColor = SystemColors.ControlText;
                        StartPreparation();
                    }
                });
            }
            catch (Exception ex)
            {
                busy = false;
                lastLog += "\n" + ex;
                try { File.WriteAllText(Path.Combine(home, "last-error.log"), lastLog); } catch { }
                Ui(() =>
                {
                    detailsButton.Visible = true;
                    Step("Something unexpected went wrong while getting the build tools ready. Close and reopen the app to try again.");
                });
            }
        }) { IsBackground = true }.Start();
    }

    void Prepare()
    {
        // Unpack the embedded maker files into a folder named after their checksum.
        byte[] kit;
        using (var s = typeof(MainForm).Assembly.GetManifestResourceStream("maker.zip"))
        using (var m = new MemoryStream()) { s.CopyTo(m); kit = m.ToArray(); }
        string id = Hex(SHA256Bytes(kit)).Substring(0, 10);
        root = Path.Combine(home, "m-" + id);
        Directory.CreateDirectory(home);

        long free = 0;
        try { free = new DriveInfo(Path.GetPathRoot(home)).AvailableFreeSpace; } catch { }
        bool needTools = !File.Exists(Path.Combine(root, "runtime", "portable-manifest.json"))
                         || !File.Exists(Path.Combine(root, "runtime", "python", "python.exe"));
        if (needTools && free > 0 && free < 2500L * 1024 * 1024)
            throw new FriendlyException(string.Format(
                "There is not enough free space on drive {0}. The build tools need about 2.5 GB the first time. " +
                "Free up some space, then try again.", Path.GetPathRoot(home)));

        if (!File.Exists(Path.Combine(root, ".unpacked")))
        {
            Ui(() => toolsLabel.Text = "Unpacking...");
            using (var zip = new ZipArchive(new MemoryStream(kit), ZipArchiveMode.Read))
            {
                foreach (var entry in zip.Entries)
                {
                    if (entry.FullName.EndsWith("/")) continue;
                    string rel = entry.FullName;
                    int slash = rel.IndexOf('/');
                    if (slash >= 0) rel = rel.Substring(slash + 1);     // drop the kit's top folder
                    string target = Path.Combine(root, rel.Replace('/', '\\'));
                    Directory.CreateDirectory(Path.GetDirectoryName(target));
                    using (var src = entry.Open())
                    using (var dst = File.Create(target)) src.CopyTo(dst);
                }
            }
            File.WriteAllText(Path.Combine(root, ".unpacked"), id);
        }
        CleanOldVersions(id);

        if (!needTools)
        {
            Ui(() => toolsLabel.Text = "Build tools: checking...");
            if (RunPython(new[] { @"tools\rom-maker\maker.py", "--check" }, null, true) == 0) return;
        }
        Ui(() => toolsLabel.Text = "Build tools: downloading (first time only)");
        Step("Getting the build tools ready. This happens once and takes a few minutes. You can choose your ROM meanwhile.");
        Progress(2);

        // 1. Python, pinned in portable-runtimes.json, downloaded and verified here.
        string runtimes = File.ReadAllText(Path.Combine(root, @"tools\rom-maker\portable-runtimes.json"));
        string win = runtimes.Substring(runtimes.IndexOf("\"windows-x64\""));
        string py = win.Substring(win.IndexOf("\"python\""));
        string url = Regex.Match(py, "\"url\"\\s*:\\s*\"([^\"]+)\"").Groups[1].Value;
        string sha = Regex.Match(py, "\"sha256\"\\s*:\\s*\"([0-9a-fA-F]{64})\"").Groups[1].Value.ToLowerInvariant();
        string boot = Path.Combine(root, @"build\bootstrap");
        Directory.CreateDirectory(boot);
        string archive = Path.Combine(boot, "python.tar.gz");
        Download(url, archive, sha, 2, 20);
        if (!File.Exists(Path.Combine(boot, @"python\python.exe")))
        {
            Step("Unpacking Python...");
            ExtractTarGz(archive, boot);
        }

        // 2. Node.js, Zig, Pillow, NumPy and the npm packages, by the maker's own packaging script.
        Step("Downloading Node.js, the compiler and libraries. This is the slow part.");
        Marquee();
        string bootPython = Path.Combine(boot, @"python\python.exe");
        int rc = Run(bootPython, new[] { @"tools\rom-maker\portable.py", "--platform", "windows-x64", "--root", root },
            line =>
            {
                if (line.StartsWith("Bundling python")) Step("Setting up Python...");
                else if (line.StartsWith("Bundling node")) Step("Downloading Node.js...");
                else if (line.StartsWith("Bundling zig")) Step("Downloading the compiler (the biggest download)...");
                else if (line.Contains("added") && line.Contains("packages")) Step("Build packages installed.");
            }, false, 3);
        if (rc != 0)
            throw new FriendlyException("The build tools could not be downloaded. Check you are connected to the internet, " +
                "then try again. Anything already downloaded is kept, so it carries on where it stopped.");
        TryDelete(boot);
        TryDelete(Path.Combine(root, @"build\portable-cache"));
        Progress(100);
        if (RunPython(new[] { @"tools\rom-maker\maker.py", "--check" }, null, false) != 0)
            throw new FriendlyException("The build tools were downloaded but did not start correctly. " +
                "Your security software may have blocked them. Try again; if it keeps happening, use \"Copy details for support\".");
    }

    void CleanOldVersions(string keep)
    {
        try
        {
            foreach (var dir in Directory.GetDirectories(home, "m-*"))
                if (!dir.EndsWith("m-" + keep)) TryDelete(dir);
        }
        catch { }
    }

    // ------------------------------------------------------------------ ROM check and build
    void CheckRom()
    {
        if (busy) return;
        var paths = romPaths;
        ShowRomStatus("Checking your ROM...", Color.DimGray);
        new Thread(() =>
        {
            var lines = new List<string>();
            var args = new List<string> { @"tools\rom-maker\maker.py" };
            args.AddRange(paths);
            args.Add("--validate-only");
            int rc = RunPython(args.ToArray(), l => lines.Add(l), true);
            romOk = rc == 0;
            ShowRomStatus(Summarise(lines, romOk), romOk ? Color.DarkGreen : Color.Firebrick);
            UpdateButtons();
        }) { IsBackground = true }.Start();
    }

    static string Summarise(List<string> lines, bool ok)
    {
        var keep = new List<string>();
        foreach (var raw in lines)
        {
            string l = raw.Trim();
            if (l.Length == 0 || l.StartsWith("Looking for") || l.StartsWith("Validated") || l.StartsWith("Searched")) continue;
            if (l.StartsWith("ROM MAKER ERROR:")) l = l.Substring(16).Trim();
            l = l.Replace("World parent 'pacland': all 19 files found in", "Pac-Land arcade ROM found in");
            l = Regex.Replace(l, @"^Japanese artwork 'paclandj': all 4 files found in (.*?) \(.*$", "Japanese artwork found in $1.");
            l = l.Replace("Japanese character option enabled.", "");
            keep.Add(l);
        }
        if (ok) keep.Insert(0, "Ready to build.");
        return string.Join("\n", keep.ToArray());
    }

    void OnBuild(object sender, EventArgs e)
    {
        busy = true;
        UpdateButtons();
        openButton.Visible = false;
        detailsButton.Visible = false;
        stepLabel.ForeColor = SystemColors.ControlText;
        chooseButton.Enabled = false;
        var paths = romPaths;
        Progress(2);
        Step("Starting...");
        new Thread(() =>
        {
            var errors = new List<string>();
            bool inError = false;
            Directory.CreateDirectory(outputDir);
            var args = new List<string> { @"tools\rom-maker\maker.py" };
            args.AddRange(paths);
            args.Add("--output");
            args.Add(outputDir);
            var watch = Stopwatch.StartNew();
            int rc = RunPython(args.ToArray(), line =>
            {
                var m = Regex.Match(line, @"^\[(\d+)/10\]\s*(.*)$");
                if (m.Success)
                {
                    int n = int.Parse(m.Groups[1].Value);
                    Progress(n * 10 - 5);
                    Step(string.Format("Step {0} of 10: {1}", n, m.Groups[2].Value));
                }
                if (line.StartsWith("ROM MAKER ERROR:")) { inError = true; errors.Add(line.Substring(16).Trim()); }
                else if (inError && line.Trim().Length > 0) errors.Add(line.Trim());
            }, false);
            busy = false;
            Ui(() => chooseButton.Enabled = true);
            if (rc == 0)
            {
                lastRom = Path.Combine(outputDir, "Pac-Land-MD.bin");
                Progress(100);
                Step(string.Format("Done in {0:0} minutes. Your ROM is \"Pac-Land-MD.bin\" in Documents\\Pac-Land MD.",
                    Math.Max(1, watch.Elapsed.TotalMinutes)));
                Ui(() => { openButton.Visible = true; });
                CleanRuns();
            }
            else
            {
                Progress(0);
                string msg = errors.Count > 0 ? string.Join("\n", errors.Take(6).ToArray())
                    : "The build stopped unexpectedly. Please try again.";
                Ui(() => { stepLabel.ForeColor = Color.Firebrick; detailsButton.Visible = true; });
                Step(msg);
            }
            UpdateButtons();
        }) { IsBackground = true }.Start();
    }

    void CleanRuns()
    {
        // Each build leaves a private working folder; keep only the newest.
        try
        {
            string runs = Path.Combine(root, @"build\rom-maker");
            if (!Directory.Exists(runs)) return;
            var dirs = Directory.GetDirectories(runs, "run-*").OrderByDescending(d => d).Skip(1);
            foreach (var d in dirs) TryDelete(d);
        }
        catch { }
    }

    void OpenOutput()
    {
        try
        {
            if (lastRom != null && File.Exists(lastRom))
                Process.Start("explorer.exe", "/select,\"" + lastRom + "\"");
            else Process.Start("explorer.exe", "\"" + outputDir + "\"");
        }
        catch { }
    }

    // ------------------------------------------------------------------ processes
    int RunPython(string[] args, Action<string> onLine, bool quiet)
    {
        return Run(Path.Combine(root, @"runtime\python\python.exe"), args, onLine, quiet, 1);
    }

    // Children get a clean environment: only what Windows itself needs, plus our
    // own tools first on PATH. Stray PYTHON*, NODE*, CC or PATH entries from
    // other software can therefore never change what runs.
    int Run(string exe, string[] args, Action<string> onLine, bool quiet, int attempts)
    {
        int rc = -1;
        for (int attempt = 1; attempt <= attempts; attempt++)
        {
            var psi = new ProcessStartInfo(exe, string.Join(" ", args.Select(Quote).ToArray()))
            {
                WorkingDirectory = root,
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                StandardOutputEncoding = Encoding.UTF8,
                StandardErrorEncoding = Encoding.UTF8,
            };
            var keep = new[] { "SystemRoot", "SystemDrive", "windir", "ComSpec", "PATHEXT", "TEMP", "TMP", "USERPROFILE",
                "USERNAME", "USERDOMAIN", "HOMEDRIVE", "HOMEPATH", "APPDATA", "LOCALAPPDATA", "ProgramData", "ProgramFiles",
                "ProgramFiles(x86)", "CommonProgramFiles", "NUMBER_OF_PROCESSORS", "PROCESSOR_ARCHITECTURE",
                "PROCESSOR_IDENTIFIER", "OS", "HTTP_PROXY", "HTTPS_PROXY", "NO_PROXY", "http_proxy", "https_proxy", "no_proxy" };
            var saved = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var k in keep) { var v = Environment.GetEnvironmentVariable(k); if (v != null) saved[k] = v; }
            psi.EnvironmentVariables.Clear();
            foreach (var kv in saved) psi.EnvironmentVariables[kv.Key] = kv.Value;
            string sys = Environment.GetFolderPath(Environment.SpecialFolder.System);
            psi.EnvironmentVariables["PATH"] = string.Join(";", new[] {
                Path.Combine(root, @"runtime\python"), Path.Combine(root, @"runtime\node"), Path.Combine(root, @"runtime\zig"),
                sys, Path.GetDirectoryName(sys), Path.Combine(sys, "Wbem") });
            psi.EnvironmentVariables["PYTHONNOUSERSITE"] = "1";
            psi.EnvironmentVariables["PYTHONIOENCODING"] = "utf-8";
            psi.EnvironmentVariables["PYTHONUTF8"] = "1";
            psi.EnvironmentVariables["PYTHONUNBUFFERED"] = "1";
            psi.EnvironmentVariables["ROM_MAKER_NO_PAUSE"] = "1";
            if (File.Exists(Path.Combine(root, @"runtime\portable-manifest.json")))
                psi.EnvironmentVariables["PYTHONPATH"] = Path.Combine(root, @"tools\rom-maker\offline");
            var log = new StringBuilder();
            using (var p = new Process { StartInfo = psi })
            {
                DataReceivedEventHandler handler = (s, e) =>
                {
                    if (e.Data == null) return;
                    lock (log) log.AppendLine(e.Data);
                    if (onLine != null) onLine(e.Data);
                };
                p.OutputDataReceived += handler;
                p.ErrorDataReceived += handler;
                try { p.Start(); }
                catch (Exception ex)
                {
                    lastLog += "\nCould not start " + exe + ": " + ex.Message;
                    if (attempt < attempts) { Thread.Sleep(3000); continue; }
                    return -1;
                }
                p.BeginOutputReadLine();
                p.BeginErrorReadLine();
                p.WaitForExit();
                p.WaitForExit();
                rc = p.ExitCode;
            }
            if (!quiet || rc != 0)
            {
                lastLog = "Command: " + exe + " " + string.Join(" ", args) + "\nExit: " + rc + "\n" + Tail(log.ToString(), 12000);
                try { File.WriteAllText(Path.Combine(home, "last-run.log"), lastLog); } catch { }
            }
            if (rc == 0) return 0;
            if (attempt < attempts) { Step("A download did not finish. Trying again..."); Thread.Sleep(5000); }
        }
        return rc;
    }

    static string Quote(string a)
    {
        if (a.Length > 0 && a.IndexOfAny(new[] { ' ', '\t', '"' }) < 0) return a;
        var sb = new StringBuilder("\"");
        int slashes = 0;
        foreach (char c in a)
        {
            if (c == '\\') { slashes++; continue; }
            if (c == '"') { sb.Append('\\', slashes * 2 + 1); slashes = 0; sb.Append('"'); continue; }
            sb.Append('\\', slashes); slashes = 0; sb.Append(c);
        }
        sb.Append('\\', slashes * 2);
        return sb.Append('"').ToString();
    }

    static string Tail(string s, int n) { return s.Length <= n ? s : s.Substring(s.Length - n); }

    // ------------------------------------------------------------------ downloads and archives
    void Download(string url, string path, string sha, int from, int to)
    {
        if (File.Exists(path) && Hex(SHA256File(path)) == sha) { Progress(to); return; }
        try { ServicePointManager.SecurityProtocol |= (SecurityProtocolType)3072; } catch { }   // TLS 1.2
        Exception last = null;
        for (int attempt = 1; attempt <= 4; attempt++)
        {
            try
            {
                using (var done = new ManualResetEvent(false))
                using (var web = new WebClient())
                {
                    web.Headers[HttpRequestHeader.UserAgent] = "PacLand-ROM-Maker/1.0";
                    web.Proxy = WebRequest.GetSystemWebProxy();
                    web.Proxy.Credentials = CredentialCache.DefaultCredentials;
                    Exception failed = null;
                    web.DownloadProgressChanged += (s, e) => Progress(from + (to - from) * e.ProgressPercentage / 100);
                    web.DownloadFileCompleted += (s, e) => { failed = e.Error; done.Set(); };
                    web.DownloadFileAsync(new Uri(url), path + ".part");
                    done.WaitOne();
                    if (failed != null) throw failed;
                }
                if (Hex(SHA256File(path + ".part")) != sha) throw new Exception("checksum mismatch");
                if (File.Exists(path)) File.Delete(path);
                File.Move(path + ".part", path);
                return;
            }
            catch (Exception ex)
            {
                last = ex;
                Step("A download did not finish. Trying again...");
                Thread.Sleep(3000 * attempt);
            }
        }
        lastLog += "\nDownload failed: " + url + "\n" + last;
        throw new FriendlyException("The build tools could not be downloaded. Check you are connected to the internet, then try again.");
    }

    // Minimal reader for the Python archive (ustar, GNU long names, pax paths).
    static void ExtractTarGz(string archive, string dest)
    {
        using (var file = File.OpenRead(archive))
        using (var gz = new GZipStream(file, CompressionMode.Decompress))
        {
            var header = new byte[512];
            string longName = null;
            while (true)
            {
                if (ReadFull(gz, header, 512) < 512) break;
                if (header.All(b => b == 0)) break;
                string name = Field(header, 0, 100);
                string prefix = Field(header, 345, 155);
                long size = Convert.ToInt64(Field(header, 124, 12).Trim().Length == 0 ? "0" : Field(header, 124, 12).Trim(), 8);
                char type = (char)header[156];
                byte[] data = new byte[size];
                ReadFull(gz, data, (int)size);
                long pad = (512 - size % 512) % 512;
                if (pad > 0) ReadFull(gz, new byte[pad], (int)pad);
                if (type == 'L') { longName = Encoding.UTF8.GetString(data).TrimEnd(new[] { '\0' }); continue; }
                if (type == 'x')
                {
                    var m = Regex.Match(Encoding.UTF8.GetString(data), @"\d+ path=([^\n]*)\n");
                    if (m.Success) longName = m.Groups[1].Value;
                    continue;
                }
                if (type == 'g') continue;
                string path = longName ?? (prefix.Length > 0 ? prefix + "/" + name : name);
                longName = null;
                if (path.Contains("..")) continue;
                string target = Path.Combine(dest, path.Replace('/', '\\'));
                if (type == '5') { Directory.CreateDirectory(target); continue; }
                if (type != '0' && type != '\0' && type != '7') continue;
                Directory.CreateDirectory(Path.GetDirectoryName(target));
                File.WriteAllBytes(target, data);
            }
        }
    }

    static int ReadFull(Stream s, byte[] buffer, int count)
    {
        int total = 0;
        while (total < count) { int n = s.Read(buffer, total, count - total); if (n <= 0) break; total += n; }
        return total;
    }

    static string Field(byte[] b, int offset, int length)
    {
        int end = offset;
        while (end < offset + length && b[end] != 0) end++;
        return Encoding.UTF8.GetString(b, offset, end - offset);
    }

    static byte[] SHA256Bytes(byte[] data) { using (var h = SHA256.Create()) return h.ComputeHash(data); }
    static byte[] SHA256File(string path) { using (var h = SHA256.Create()) using (var f = File.OpenRead(path)) return h.ComputeHash(f); }
    static string Hex(byte[] b) { return BitConverter.ToString(b).Replace("-", "").ToLowerInvariant(); }
    static void TryDelete(string dir) { try { if (Directory.Exists(dir)) Directory.Delete(dir, true); } catch { } }
}

class FriendlyException : Exception { public FriendlyException(string m) : base(m) { } }
