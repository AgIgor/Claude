using System;
using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;

namespace MonitorSerial
{
    static class Program
    {
        [STAThread]
        static void Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.ThreadException += delegate(object s, System.Threading.ThreadExceptionEventArgs e)
            {
                MessageBox.Show(e.Exception.Message, "Monitor Serial - erro", MessageBoxButtons.OK, MessageBoxIcon.Error);
            };

            Settings.Load();

            // Argumentos opcionais: MonitorSerial.exe COM3 COM4:115200 ...
            // Cada argumento abre uma janela já conectada.
            bool opened = false;
            foreach (string a in args)
            {
                string p = a.Trim();
                int baud = 0;
                int i = p.IndexOfAny(new[] { ':', '@' });
                if (i > 0)
                {
                    int.TryParse(p.Substring(i + 1), out baud);
                    p = p.Substring(0, i);
                }
                if (p.Length == 0) continue;
                WindowManager.Open(p.ToUpperInvariant(), baud, true, null);
                opened = true;
            }
            if (!opened) WindowManager.Open(null, 0, false, null);

            Application.Run(WindowManager.Context);
        }
    }

    /// <summary>Controla as várias janelas abertas no mesmo processo e quais portas estão em uso.</summary>
    static class WindowManager
    {
        public static readonly ApplicationContext Context = new ApplicationContext();
        static readonly List<MonitorForm> forms = new List<MonitorForm>();
        static readonly Dictionary<string, MonitorForm> openPorts =
            new Dictionary<string, MonitorForm>(StringComparer.OrdinalIgnoreCase);

        static Icon appIcon;
        static bool iconLoaded;

        public static int Count { get { return forms.Count; } }

        public static Icon AppIcon
        {
            get
            {
                if (!iconLoaded)
                {
                    iconLoaded = true;
                    try { appIcon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); }
                    catch { appIcon = null; }
                }
                return appIcon;
            }
        }

        public static MonitorForm Open(string port, int baud, bool autoConnect, Form near)
        {
            var f = new MonitorForm(port, baud, autoConnect);
            if (near == null && forms.Count > 0) near = forms[forms.Count - 1];
            if (near != null)
            {
                Rectangle wa = Screen.FromControl(near).WorkingArea;
                var p = new Point(near.Left + 32, near.Top + 32);
                if (p.X + f.Width > wa.Right || p.Y + f.Height > wa.Bottom)
                    p = new Point(wa.Left + 20, wa.Top + 20);
                f.StartPosition = FormStartPosition.Manual;
                f.Location = p;
            }
            forms.Add(f);
            f.FormClosed += delegate
            {
                forms.Remove(f);
                if (forms.Count == 0) Context.ExitThread();
            };
            f.Show();
            return f;
        }

        public static bool IsOpenElsewhere(string port, MonitorForm me)
        {
            MonitorForm f;
            return port != null && openPorts.TryGetValue(port, out f) && f != me;
        }

        public static void SetOpen(string port, MonitorForm owner, bool isOpen)
        {
            if (port == null) return;
            if (isOpen) openPorts[port] = owner;
            else
            {
                MonitorForm cur;
                if (openPorts.TryGetValue(port, out cur) && cur == owner) openPorts.Remove(port);
            }
            foreach (var f in forms.ToArray())
                if (f != owner) f.PortsInUseChanged();
        }

        /// <summary>Primeira porta que nenhuma janela está usando ou com selecionada.</summary>
        public static string FirstUnusedPort(string[] names)
        {
            foreach (string n in names)
            {
                if (openPorts.ContainsKey(n)) continue;
                bool taken = false;
                foreach (var f in forms)
                    if (string.Equals(f.SelectedPortName(), n, StringComparison.OrdinalIgnoreCase)) { taken = true; break; }
                if (!taken) return n;
            }
            return names.Length > 0 ? names[0] : null;
        }
    }
}
