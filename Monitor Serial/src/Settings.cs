using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Ports;
using System.Management;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows.Forms;

namespace MonitorSerial
{
    /// <summary>
    /// Configurações simples em arquivo chave=valor. Fica em %APPDATA%\MonitorSerial,
    /// ou ao lado do .exe se já existir um config.ini lá (modo portátil).
    /// </summary>
    static class Settings
    {
        static string dir;
        static readonly Dictionary<string, string> values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        public static readonly List<string> History = new List<string>();
        const int MaxHistory = 200;

        static string ConfigFile { get { return Path.Combine(dir, "config.ini"); } }
        static string HistoryFile { get { return Path.Combine(dir, "historico.txt"); } }

        public static void Load()
        {
            string exeDir = Path.GetDirectoryName(Application.ExecutablePath);
            dir = File.Exists(Path.Combine(exeDir, "config.ini"))
                ? exeDir
                : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "MonitorSerial");
            try
            {
                if (File.Exists(ConfigFile))
                {
                    foreach (string line in File.ReadAllLines(ConfigFile, Encoding.UTF8))
                    {
                        int i = line.IndexOf('=');
                        if (i > 0) values[line.Substring(0, i).Trim()] = line.Substring(i + 1);
                    }
                }
                if (File.Exists(HistoryFile))
                {
                    foreach (string line in File.ReadAllLines(HistoryFile, Encoding.UTF8))
                        if (line.Length > 0 && History.Count < MaxHistory) History.Add(line);
                }
            }
            catch { }
        }

        public static void Save()
        {
            try
            {
                Directory.CreateDirectory(dir);
                var lines = new List<string>();
                foreach (var kv in values) lines.Add(kv.Key + "=" + kv.Value);
                lines.Sort(StringComparer.OrdinalIgnoreCase);
                File.WriteAllLines(ConfigFile, lines.ToArray(), new UTF8Encoding(false));
            }
            catch { }
        }

        public static void AddHistory(string cmd)
        {
            if (string.IsNullOrEmpty(cmd)) return;
            History.Remove(cmd);
            History.Insert(0, cmd);
            while (History.Count > MaxHistory) History.RemoveAt(History.Count - 1);
            try
            {
                Directory.CreateDirectory(dir);
                File.WriteAllLines(HistoryFile, History.ToArray(), new UTF8Encoding(false));
            }
            catch { }
        }

        public static string Get(string key, string def)
        {
            string v;
            return values.TryGetValue(key, out v) ? v : def;
        }

        public static int GetInt(string key, int def)
        {
            int v;
            return int.TryParse(Get(key, null), NumberStyles.Integer, CultureInfo.InvariantCulture, out v) ? v : def;
        }

        public static bool GetBool(string key, bool def)
        {
            string v = Get(key, null);
            return v == null ? def : v == "1";
        }

        public static void Set(string key, object value)
        {
            if (value is bool) value = (bool)value ? "1" : "0";
            values[key] = Convert.ToString(value, CultureInfo.InvariantCulture) ?? "";
        }
    }

    /// <summary>Lista de portas COM com o nome amigável do dispositivo (ex.: "USB-SERIAL CH340").</summary>
    static class PortInfo
    {
        static string descKey;
        static Dictionary<string, string> desc = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

        public static string[] GetNames()
        {
            string[] raw;
            try { raw = SerialPort.GetPortNames(); }
            catch { raw = new string[0]; }

            var list = new List<string>();
            foreach (string r in raw)
            {
                // Alguns drivers devolvem lixo depois do número (ex.: "COM3\0" ou "COM3c").
                Match m = Regex.Match(r, @"^(COM\d+)", RegexOptions.IgnoreCase);
                string n = m.Success ? m.Groups[1].Value.ToUpperInvariant() : r;
                if (!list.Contains(n)) list.Add(n);
            }
            list.Sort(delegate(string a, string b)
            {
                int c = PortNumber(a).CompareTo(PortNumber(b));
                return c != 0 ? c : string.Compare(a, b, StringComparison.OrdinalIgnoreCase);
            });
            return list.ToArray();
        }

        static int PortNumber(string name)
        {
            Match m = Regex.Match(name, @"\d+");
            int n;
            return m.Success && int.TryParse(m.Value, out n) ? n : int.MaxValue;
        }

        /// <summary>Consulta o WMI só quando o conjunto de portas muda.</summary>
        public static Dictionary<string, string> Descriptions(string[] names)
        {
            string key = string.Join(",", names);
            if (key == descKey) return desc;
            var d = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            try
            {
                using (var s = new ManagementObjectSearcher("SELECT Name FROM Win32_PnPEntity WHERE Name LIKE '%(COM%'"))
                {
                    foreach (ManagementBaseObject mo in s.Get())
                    {
                        string n = mo["Name"] as string;
                        if (n == null) continue;
                        int i = n.LastIndexOf("(COM", StringComparison.OrdinalIgnoreCase);
                        if (i < 0) continue;
                        int j = n.IndexOf(')', i);
                        if (j < 0) continue;
                        d[n.Substring(i + 1, j - i - 1)] = n.Substring(0, i).Trim();
                    }
                }
            }
            catch { }
            desc = d;
            descKey = key;
            return desc;
        }
    }
}
