using System;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.IO.Ports;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace MonitorSerial
{
    sealed class PortItem
    {
        public string Name;
        public string Desc;
        public bool InUse;

        public override string ToString()
        {
            string s = Name;
            if (!string.IsNullOrEmpty(Desc)) s += "  -  " + Desc;
            if (InUse) s += "  [em uso]";
            return s;
        }
    }

    sealed class Palette
    {
        public readonly Color Back, Rx, Ts, Sent, Sys;

        public Palette(Color back, Color rx, Color ts, Color sent, Color sys)
        {
            Back = back; Rx = rx; Ts = ts; Sent = sent; Sys = sys;
        }
    }

    sealed class Seg
    {
        public int Kind;
        public readonly StringBuilder Text = new StringBuilder();
    }

    /// <summary>Trechos de texto coloridos; trechos seguidos do mesmo tipo são unidos.</summary>
    sealed class SegList : List<Seg>
    {
        public void Add(int kind, string s)
        {
            if (s.Length > 0) Last(kind).Text.Append(s);
        }

        public void Add(int kind, char c)
        {
            Last(kind).Text.Append(c);
        }

        Seg Last(int kind)
        {
            if (Count > 0 && this[Count - 1].Kind == kind) return this[Count - 1];
            var s = new Seg();
            s.Kind = kind;
            Add(s);
            return s;
        }
    }

    struct Chunk
    {
        public DateTime Time;
        public byte[] Data;
    }

    public sealed class MonitorForm : Form
    {
        static readonly int[] BaudRates =
        {
            300, 600, 750, 1200, 2400, 4800, 9600, 14400, 19200, 28800, 31250, 38400, 57600,
            74880, 115200, 230400, 250000, 460800, 500000, 921600, 1000000, 2000000
        };
        static readonly string[] TsModes =
        {
            "Desligado", "Hora (HH:mm:ss.fff)", "Hora (HH:mm:ss)", "Data e hora",
            "Segundos desde a conexão", "Delta entre linhas (ms)"
        };
        static readonly string[] LineEnds = { "Sem final de linha", "Nova linha (LF)", "Retorno de carro (CR)", "Ambos NL & CR" };
        static readonly string[] LineEndValues = { "", "\n", "\r", "\r\n" };
        static readonly string[] HexTable = BuildHexTable();

        static readonly Palette LightPal = new Palette(Color.White, Color.FromArgb(20, 20, 20),
            Color.FromArgb(128, 128, 128), Color.FromArgb(0, 95, 200), Color.FromArgb(190, 100, 0));
        static readonly Palette DarkPal = new Palette(Color.FromArgb(30, 30, 30), Color.FromArgb(220, 220, 220),
            Color.FromArgb(120, 120, 120), Color.FromArgb(86, 156, 214), Color.FromArgb(215, 155, 95));

        const int KRx = 0, KTs = 1, KSent = 2, KSys = 3;
        const int MaxChars = 3000000;

        // ---- controles
        ComboBox cbPort, cbBaud, cbData, cbParity, cbStop, cbFlow, cbEnc, cbTs, cbView, cbSend, cbLineEnd;
        CheckBox chkDtr, chkRts, chkReconnect, chkWrap, chkAnsi, chkAutoscroll, chkLog, chkTop, chkDark;
        CheckBox chkHex, chkEsc, chkEcho, chkRepeat;
        Button btnRefresh, btnConnect, btnAdv, btnNew, btnPause, btnClear, btnSave, btnFind, btnSend, btnFile;
        NumericUpDown numMaxLines, numRepeat;
        TextBox txtFind;
        RichTextBox output;
        FlowLayoutPanel pnlAdv;
        ToolStripStatusLabel lblStatus, lblRx, lblTx, lblLines;
        ToolTip tip;
        System.Windows.Forms.Timer uiTimer, scanTimer, repeatTimer;

        // ---- estado da porta
        volatile SerialPort port;
        string targetPort;          // porta que queremos manter conectada (reconexão automática)
        bool wantConnected;
        int seenTicks;
        bool autoConnectOnShow;

        readonly object rxLock = new object();
        List<Chunk> rxQueue = new List<Chunk>();
        long rxBytes, txBytes, rxLines;
        long shownRx = -1, shownTx = -1, shownLines = -1;

        // ---- estado da exibição
        bool atLineStart = true, lastWasCR;
        int hexCol, escState, tsMode, viewMode;
        DateTime connectTime = DateTime.Now, lastLineTime = DateTime.Now;
        Encoding encoding = Encoding.UTF8;
        Decoder decoder = Encoding.UTF8.GetDecoder();
        bool paused, loading, updatingPorts, suppressLog;
        StreamWriter logWriter;
        bool logDirty;
        Palette pal = LightPal;
        string lastPortKey;
        string[] lastNames = new string[0];
        int histIndex = -1;
        string histDraft = "";

        public MonitorForm(string initialPort, int initialBaud, bool autoConnect)
        {
            loading = true;
            BuildUi();
            LoadSettings();
            if (initialBaud > 0) cbBaud.Text = initialBaud.ToString(CultureInfo.InvariantCulture);

            RefreshPorts(true);
            string p = initialPort;
            if (p == null && WindowManager.Count == 0) p = Settings.Get("porta", null);
            if (p == null || Array.IndexOf(lastNames, p) < 0 && initialPort == null) p = WindowManager.FirstUnusedPort(lastNames);
            SelectPort(p);
            targetPort = p;
            loading = false;

            autoConnectOnShow = autoConnect && p != null;
            WireEvents();
            ApplyTheme(chkDark.Checked, false);
            UpdateConnState();

            uiTimer = new System.Windows.Forms.Timer { Interval = 30 };
            uiTimer.Tick += delegate { UiTick(); };
            uiTimer.Start();
            scanTimer = new System.Windows.Forms.Timer { Interval = 1000 };
            scanTimer.Tick += delegate { ScanTick(); };
            scanTimer.Start();
            repeatTimer = new System.Windows.Forms.Timer { Interval = (int)numRepeat.Value };
            repeatTimer.Tick += delegate { if (port != null && cbSend.Text.Length > 0) DoSend(true); };
        }

        // =====================================================================
        //  Interface
        // =====================================================================

        void BuildUi()
        {
            SuspendLayout();
            Font = new Font("Segoe UI", 9f);
            Text = "Monitor Serial";
            if (WindowManager.AppIcon != null) Icon = WindowManager.AppIcon;
            Size = new Size(Settings.GetInt("largura", 980), Settings.GetInt("altura", 620));
            MinimumSize = new Size(600, 380);
            tip = new ToolTip();

            var main = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 6, Padding = new Padding(4, 2, 4, 0) };
            main.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            for (int i = 0; i < 6; i++)
                main.RowStyles.Add(i == 3 ? new RowStyle(SizeType.Percent, 100f) : new RowStyle(SizeType.AutoSize));

            // ---- linha 1: porta e conexão
            var r1 = NewRow();
            cbPort = Cmb(250);
            btnRefresh = Btn("Atualizar");
            btnConnect = Btn("Conectar");
            btnConnect.MinimumSize = new Size(96, 0);
            btnConnect.Font = new Font(Font, FontStyle.Bold);
            cbBaud = Cmb(92);
            cbBaud.DropDownStyle = ComboBoxStyle.DropDown;
            foreach (int b in BaudRates) cbBaud.Items.Add(b.ToString(CultureInfo.InvariantCulture));
            chkDtr = Chk("DTR");
            chkRts = Chk("RTS");
            btnAdv = Btn("Avançado ▾");
            btnNew = Btn("Nova janela");
            r1.Controls.AddRange(new Control[] { Lbl("Porta:", 0), cbPort, btnRefresh, btnConnect, Lbl("Baud:", 12), cbBaud, chkDtr, chkRts, btnAdv, btnNew });

            // ---- linha 2: configurações avançadas (pode ser escondida)
            pnlAdv = NewRow();
            cbData = Cmb(42, "5", "6", "7", "8");
            cbParity = Cmb(76, "Nenhuma", "Par", "Ímpar", "Marca", "Espaço");
            cbStop = Cmb(46, "1", "1.5", "2");
            cbFlow = Cmb(118, "Nenhum", "RTS/CTS", "XON/XOFF", "RTS/CTS + XON/XOFF");
            cbEnc = Cmb(104, "UTF-8", "ASCII", "Windows-1252", "ISO-8859-1");
            chkReconnect = Chk("Reconectar automaticamente");
            chkAnsi = Chk("Remover cores ANSI");
            chkWrap = Chk("Quebrar linhas");
            numMaxLines = new NumericUpDown { Minimum = 100, Maximum = 1000000, Increment = 1000, Width = 78, Margin = new Padding(3, 4, 3, 3), ThousandsSeparator = true };
            pnlAdv.Controls.AddRange(new Control[] {
                Lbl("Dados:", 0), cbData, Lbl("Paridade:", 6), cbParity, Lbl("Parada:", 6), cbStop,
                Lbl("Fluxo:", 6), cbFlow, Lbl("Codificação:", 6), cbEnc, chkReconnect, chkAnsi, chkWrap,
                Lbl("Máx. linhas:", 6), numMaxLines, chkTop, chkDark });

            // ---- linha 3: exibição
            var r3 = NewRow();
            cbTs = Cmb(160, TsModes);
            cbView = Cmb(100, "Texto", "Hexadecimal");
            chkAutoscroll = Chk("Autoscroll");
            btnPause = Btn("Pausar");
            btnClear = Btn("Limpar");
            btnSave = Btn("Salvar...");
            chkLog = Chk("Gravar log");
            txtFind = new TextBox { Width = 110, Margin = new Padding(14, 4, 0, 3) };
            btnFind = Btn("Buscar");
            chkTop = Chk("Sempre no topo");
            chkDark = Chk("Tema escuro");
            r3.Controls.AddRange(new Control[] {
                Lbl("Timestamp:", 0), cbTs, Lbl("Exibir:", 6), cbView, chkAutoscroll, btnPause, btnClear, btnSave, chkLog,
                txtFind, btnFind });

            // ---- saída
            output = new RichTextBox
            {
                Dock = DockStyle.Fill,
                ReadOnly = true,
                DetectUrls = false,
                HideSelection = false,
                BorderStyle = BorderStyle.FixedSingle,
                Font = new Font("Consolas", 10f),
                Margin = new Padding(3, 2, 3, 2),
                BackColor = Color.White,
            };
            var menu = new ContextMenuStrip();
            menu.Items.Add("Copiar", null, delegate { output.Copy(); });
            menu.Items.Add("Selecionar tudo", null, delegate { output.SelectAll(); });
            menu.Items.Add(new ToolStripSeparator());
            menu.Items.Add("Limpar (Ctrl+L)", null, delegate { ClearOutput(); });
            menu.Items.Add("Salvar... (Ctrl+S)", null, delegate { SaveOutput(); });
            output.ContextMenuStrip = menu;

            // ---- linha de envio
            var sendRow = new TableLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, ColumnCount = 3, RowCount = 1, Margin = new Padding(0) };
            sendRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            sendRow.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            sendRow.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            cbSend = new ComboBox { Anchor = AnchorStyles.Left | AnchorStyles.Right, Font = new Font("Consolas", 10f), MaxDropDownItems = 20, Margin = new Padding(3, 4, 3, 3) };
            cbLineEnd = Cmb(150, LineEnds);
            cbLineEnd.Anchor = AnchorStyles.Left;
            btnSend = Btn("Enviar");
            btnSend.MinimumSize = new Size(80, 0);
            btnSend.Font = new Font(Font, FontStyle.Bold);
            sendRow.Controls.Add(cbSend, 0, 0);
            sendRow.Controls.Add(cbLineEnd, 1, 0);
            sendRow.Controls.Add(btnSend, 2, 0);

            // ---- opções de envio
            var sendOpts = NewRow();
            chkHex = Chk("Enviar em HEX");
            chkEsc = Chk(@"Interpretar \n \r \t \xHH");
            chkEcho = Chk("Eco local");
            chkRepeat = Chk("Repetir a cada");
            numRepeat = new NumericUpDown { Minimum = 10, Maximum = 3600000, Increment = 100, Width = 74, Margin = new Padding(0, 4, 0, 3) };
            btnFile = Btn("Enviar arquivo...");
            sendOpts.Controls.AddRange(new Control[] { chkHex, chkEsc, chkEcho, chkRepeat, numRepeat, Lbl("ms", 2), btnFile });

            main.Controls.Add(r1, 0, 0);
            main.Controls.Add(pnlAdv, 0, 1);
            main.Controls.Add(r3, 0, 2);
            main.Controls.Add(output, 0, 3);
            main.Controls.Add(sendRow, 0, 4);
            main.Controls.Add(sendOpts, 0, 5);

            // ---- barra de status
            var status = new StatusStrip { SizingGrip = true };
            lblStatus = new ToolStripStatusLabel { Spring = true, TextAlign = ContentAlignment.MiddleLeft };
            lblRx = new ToolStripStatusLabel { BorderSides = ToolStripStatusLabelBorderSides.Left, ToolTipText = "Bytes recebidos" };
            lblTx = new ToolStripStatusLabel { BorderSides = ToolStripStatusLabelBorderSides.Left, ToolTipText = "Bytes enviados" };
            lblLines = new ToolStripStatusLabel { BorderSides = ToolStripStatusLabelBorderSides.Left, ToolTipText = "Linhas recebidas" };
            status.Items.AddRange(new ToolStripItem[] { lblStatus, lblRx, lblTx, lblLines });

            Controls.Add(main);
            Controls.Add(status);

            tip.SetToolTip(btnConnect, "Conectar / desconectar (F5)");
            tip.SetToolTip(btnRefresh, "Atualizar a lista de portas (a lista também se atualiza sozinha)");
            tip.SetToolTip(cbBaud, "Escolha da lista ou digite um valor e tecle Enter");
            tip.SetToolTip(chkDtr, "Data Terminal Ready. Ligado ao abrir a porta reinicia a maioria das placas Arduino.");
            tip.SetToolTip(chkRts, "Request To Send");
            tip.SetToolTip(btnNew, "Abrir outra janela para outra porta (Ctrl+N)");
            tip.SetToolTip(btnPause, "Pausar a exibição sem perder dados (Ctrl+P)");
            tip.SetToolTip(btnClear, "Limpar a saída (Ctrl+L)");
            tip.SetToolTip(btnSave, "Salvar o conteúdo da saída em arquivo (Ctrl+S)");
            tip.SetToolTip(chkLog, "Gravar continuamente tudo que aparece na saída em um arquivo");
            tip.SetToolTip(txtFind, "Ctrl+F para buscar, Enter ou F3 para a próxima ocorrência");
            tip.SetToolTip(chkReconnect, "Se o dispositivo for desconectado (ex.: cabo USB), reconecta quando ele voltar");
            tip.SetToolTip(chkAnsi, "Remove códigos de cor de terminal (ex.: logs do ESP-IDF)");
            tip.SetToolTip(cbSend, "Enter envia. Setas para cima/baixo navegam no histórico.");
            tip.SetToolTip(chkHex, "Ex.: 48 65 6C 6C 6F  ou  0x01,0x02  ou  A0B1C2");
            tip.SetToolTip(chkEsc, @"Permite \n, \r, \t, \0, \\ e \xHH (byte em hexadecimal) no texto enviado");
            tip.SetToolTip(chkEcho, "Mostrar na saída os comandos enviados");
            tip.SetToolTip(btnFile, "Enviar o conteúdo bruto de um arquivo");

            AutoScaleDimensions = new SizeF(96f, 96f);
            AutoScaleMode = AutoScaleMode.Dpi;
            ResumeLayout(false);
            PerformLayout();
        }

        static FlowLayoutPanel NewRow()
        {
            return new FlowLayoutPanel
            {
                AutoSize = true,
                AutoSizeMode = AutoSizeMode.GrowAndShrink,
                Dock = DockStyle.Fill,
                WrapContents = true,
                Margin = new Padding(0),
            };
        }

        static Label Lbl(string text, int left)
        {
            return new Label { Text = text, AutoSize = true, Margin = new Padding(left, 7, 0, 0) };
        }

        static Button Btn(string text)
        {
            return new Button
            {
                Text = text,
                AutoSize = true,
                AutoSizeMode = AutoSizeMode.GrowAndShrink,
                MinimumSize = new Size(0, 25),
                Padding = new Padding(2, 0, 2, 0),
                Margin = new Padding(3, 3, 3, 3),
                UseVisualStyleBackColor = true
            };
        }

        static ComboBox Cmb(int width, params string[] items)
        {
            var c = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = width, Margin = new Padding(3, 4, 3, 3) };
            c.Items.AddRange(items);
            return c;
        }

        static CheckBox Chk(string text)
        {
            return new CheckBox { Text = text, AutoSize = true, Margin = new Padding(8, 6, 3, 0) };
        }

        static void SetIdx(ComboBox c, int i)
        {
            c.SelectedIndex = Math.Max(0, Math.Min(c.Items.Count - 1, i));
        }

        void LoadSettings()
        {
            cbBaud.Text = Settings.Get("baud", "9600");
            SetIdx(cbData, Settings.GetInt("dados", 3));
            SetIdx(cbParity, Settings.GetInt("paridade", 0));
            SetIdx(cbStop, Settings.GetInt("parada", 0));
            SetIdx(cbFlow, Settings.GetInt("fluxo", 0));
            SetIdx(cbEnc, Settings.GetInt("codificacao", 0));
            SetIdx(cbTs, Settings.GetInt("timestamp", 0));
            SetIdx(cbView, Settings.GetInt("exibir", 0));
            SetIdx(cbLineEnd, Settings.GetInt("finallinha", 1));
            chkDtr.Checked = Settings.GetBool("dtr", true);
            chkRts.Checked = Settings.GetBool("rts", true);
            chkReconnect.Checked = Settings.GetBool("reconectar", true);
            chkAnsi.Checked = Settings.GetBool("ansi", true);
            chkWrap.Checked = Settings.GetBool("quebra", true);
            chkAutoscroll.Checked = Settings.GetBool("autoscroll", true);
            chkDark.Checked = Settings.GetBool("escuro", false);
            chkHex.Checked = Settings.GetBool("enviarhex", false);
            chkEsc.Checked = Settings.GetBool("escapes", false);
            chkEcho.Checked = Settings.GetBool("eco", true);
            numMaxLines.Value = Math.Max(numMaxLines.Minimum, Math.Min(numMaxLines.Maximum, Settings.GetInt("maxlinhas", 20000)));
            numRepeat.Value = Math.Max(numRepeat.Minimum, Math.Min(numRepeat.Maximum, Settings.GetInt("repetirms", 1000)));
            pnlAdv.Visible = Settings.GetBool("avancado", false);
            btnAdv.Text = pnlAdv.Visible ? "Avançado ▴" : "Avançado ▾";

            output.WordWrap = chkWrap.Checked;
            tsMode = cbTs.SelectedIndex;
            viewMode = cbView.SelectedIndex;
            encoding = GetEncoding();
            decoder = encoding.GetDecoder();
            foreach (string h in Settings.History) cbSend.Items.Add(h);
        }

        void SaveSettings()
        {
            Settings.Set("porta", SelectedPortName() ?? "");
            Settings.Set("baud", cbBaud.Text.Trim());
            Settings.Set("dados", cbData.SelectedIndex);
            Settings.Set("paridade", cbParity.SelectedIndex);
            Settings.Set("parada", cbStop.SelectedIndex);
            Settings.Set("fluxo", cbFlow.SelectedIndex);
            Settings.Set("codificacao", cbEnc.SelectedIndex);
            Settings.Set("timestamp", cbTs.SelectedIndex);
            Settings.Set("exibir", cbView.SelectedIndex);
            Settings.Set("finallinha", cbLineEnd.SelectedIndex);
            Settings.Set("dtr", chkDtr.Checked);
            Settings.Set("rts", chkRts.Checked);
            Settings.Set("reconectar", chkReconnect.Checked);
            Settings.Set("ansi", chkAnsi.Checked);
            Settings.Set("quebra", chkWrap.Checked);
            Settings.Set("autoscroll", chkAutoscroll.Checked);
            Settings.Set("escuro", chkDark.Checked);
            Settings.Set("enviarhex", chkHex.Checked);
            Settings.Set("escapes", chkEsc.Checked);
            Settings.Set("eco", chkEcho.Checked);
            Settings.Set("maxlinhas", (int)numMaxLines.Value);
            Settings.Set("repetirms", (int)numRepeat.Value);
            Settings.Set("avancado", pnlAdv.Visible);
            if (WindowState == FormWindowState.Normal)
            {
                // Guarda o tamanho em "pixels de 96 dpi" para não crescer a cada execução em telas com escala.
                float scale = 1f;
                using (Graphics g = CreateGraphics()) scale = g.DpiX / 96f;
                Settings.Set("largura", (int)(Width / scale));
                Settings.Set("altura", (int)(Height / scale));
            }
            Settings.Save();
        }

        void WireEvents()
        {
            btnConnect.Click += delegate { ToggleConnect(); };
            btnRefresh.Click += delegate { lastPortKey = null; RefreshPorts(true); };
            btnNew.Click += delegate { WindowManager.Open(null, 0, false, this); };
            btnAdv.Click += delegate
            {
                pnlAdv.Visible = !pnlAdv.Visible;
                btnAdv.Text = pnlAdv.Visible ? "Avançado ▴" : "Avançado ▾";
            };
            cbPort.SelectedIndexChanged += delegate { PortSelectionChanged(); };

            cbBaud.SelectedIndexChanged += delegate { ApplyPortParam("baud"); };
            cbBaud.Leave += delegate { ApplyPortParam("baud"); };
            cbData.SelectedIndexChanged += delegate { ApplyPortParam("data"); };
            cbParity.SelectedIndexChanged += delegate { ApplyPortParam("parity"); };
            cbStop.SelectedIndexChanged += delegate { ApplyPortParam("stop"); };
            cbFlow.SelectedIndexChanged += delegate { ApplyPortParam("flow"); };
            chkDtr.CheckedChanged += delegate { ApplyPortParam("dtr"); };
            chkRts.CheckedChanged += delegate { ApplyPortParam("rts"); };
            chkReconnect.CheckedChanged += delegate { UpdateConnState(); };

            cbEnc.SelectedIndexChanged += delegate
            {
                encoding = GetEncoding();
                decoder = encoding.GetDecoder();
            };
            cbTs.SelectedIndexChanged += delegate { tsMode = cbTs.SelectedIndex; };
            cbView.SelectedIndexChanged += delegate
            {
                if (!paused) ProcessQueue();
                var segs = new SegList();
                BreakLine(segs);
                Append(segs);
                viewMode = cbView.SelectedIndex;
                decoder.Reset();
                escState = 0;
            };
            chkWrap.CheckedChanged += delegate { output.WordWrap = chkWrap.Checked; };
            chkAutoscroll.CheckedChanged += delegate
            {
                if (chkAutoscroll.Checked) { output.Select(output.TextLength, 0); output.ScrollToCaret(); }
            };
            chkTop.CheckedChanged += delegate { TopMost = chkTop.Checked; };
            chkDark.CheckedChanged += delegate { ApplyTheme(chkDark.Checked, true); };
            chkLog.CheckedChanged += delegate { LogToggled(); };

            btnPause.Click += delegate { TogglePause(); };
            btnClear.Click += delegate { ClearOutput(); };
            btnSave.Click += delegate { SaveOutput(); };
            btnFind.Click += delegate { FindNext(); };

            btnSend.Click += delegate { DoSend(false); cbSend.Focus(); };
            btnFile.Click += delegate { SendFile(); };
            cbSend.DropDown += delegate
            {
                string t = cbSend.Text;
                cbSend.BeginUpdate();
                cbSend.Items.Clear();
                foreach (string h in Settings.History) cbSend.Items.Add(h);
                cbSend.EndUpdate();
                cbSend.Text = t;
            };
            chkRepeat.CheckedChanged += delegate
            {
                repeatTimer.Interval = (int)numRepeat.Value;
                repeatTimer.Enabled = chkRepeat.Checked;
            };
            numRepeat.ValueChanged += delegate { repeatTimer.Interval = (int)numRepeat.Value; };

            // Digitar com o foco na saída leva o texto para a caixa de envio.
            output.KeyPress += delegate(object s, KeyPressEventArgs e)
            {
                if (char.IsControl(e.KeyChar)) return;
                cbSend.Focus();
                cbSend.Text += e.KeyChar;
                cbSend.SelectionStart = cbSend.Text.Length;
                e.Handled = true;
            };
        }

        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            if (autoConnectOnShow) Connect(false);
            else AddSys("Selecione a porta e clique em Conectar (F5). Ctrl+N abre outra janela.");
            cbSend.Focus();
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            base.OnFormClosing(e);
            if (e.Cancel) return;
            SaveSettings();
            uiTimer.Stop();
            scanTimer.Stop();
            repeatTimer.Stop();
            var p = port;
            if (p != null)
            {
                port = null;
                p.DataReceived -= OnDataReceived;
                ThreadPool.QueueUserWorkItem(delegate { try { p.Close(); } catch { } });
                WindowManager.SetOpen(p.PortName, this, false);
            }
            CloseLog();
        }

        protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
        {
            switch (keyData)
            {
                case Keys.Control | Keys.N: WindowManager.Open(null, 0, false, this); return true;
                case Keys.Control | Keys.L: ClearOutput(); return true;
                case Keys.Control | Keys.S: SaveOutput(); return true;
                case Keys.Control | Keys.P: TogglePause(); return true;
                case Keys.Control | Keys.F: txtFind.Focus(); txtFind.SelectAll(); return true;
                case Keys.F3: FindNext(); return true;
                case Keys.F5: ToggleConnect(); return true;
            }
            if (cbSend.ContainsFocus && !cbSend.DroppedDown)
            {
                if (keyData == Keys.Enter) { DoSend(false); return true; }
                if (keyData == Keys.Up) { HistoryNav(1); return true; }
                if (keyData == Keys.Down) { HistoryNav(-1); return true; }
            }
            if (keyData == Keys.Enter && txtFind.Focused) { FindNext(); return true; }
            if (keyData == Keys.Enter && cbBaud.ContainsFocus) { ApplyPortParam("baud"); return true; }
            return base.ProcessCmdKey(ref msg, keyData);
        }

        // =====================================================================
        //  Portas
        // =====================================================================

        public string SelectedPortName()
        {
            var it = cbPort.SelectedItem as PortItem;
            return it == null ? null : it.Name;
        }

        void SelectPort(string name)
        {
            if (name == null) return;
            for (int i = 0; i < cbPort.Items.Count; i++)
            {
                if (string.Equals(((PortItem)cbPort.Items[i]).Name, name, StringComparison.OrdinalIgnoreCase))
                {
                    cbPort.SelectedIndex = i;
                    return;
                }
            }
        }

        public void PortsInUseChanged()
        {
            if (!IsDisposed) RefreshPorts(true);
        }

        void RefreshPorts(bool force)
        {
            string[] names = PortInfo.GetNames();
            string key = string.Join(",", names);
            if (!force && key == lastPortKey) return;
            lastPortKey = key;
            lastNames = names;

            Dictionary<string, string> desc = PortInfo.Descriptions(names);
            string sel = SelectedPortName() ?? targetPort;
            updatingPorts = true;
            cbPort.BeginUpdate();
            cbPort.Items.Clear();
            foreach (string n in names)
            {
                string d;
                desc.TryGetValue(n, out d);
                cbPort.Items.Add(new PortItem { Name = n, Desc = d, InUse = WindowManager.IsOpenElsewhere(n, this) });
            }
            // Mantém visível a porta escolhida mesmo se o dispositivo foi desconectado.
            if (sel != null && Array.IndexOf(names, sel) < 0)
                cbPort.Items.Add(new PortItem { Name = sel, Desc = "não encontrada" });
            cbPort.EndUpdate();
            SelectPort(sel);
            if (cbPort.SelectedIndex < 0 && cbPort.Items.Count > 0) cbPort.SelectedIndex = 0;
            updatingPorts = false;
        }

        void PortSelectionChanged()
        {
            if (loading || updatingPorts) return;
            string n = SelectedPortName();
            var p = port;
            if (p != null && n != null && !string.Equals(n, p.PortName, StringComparison.OrdinalIgnoreCase))
            {
                Disconnect(false);
                Connect(false);
            }
            else if (p == null && wantConnected)
            {
                targetPort = n;
            }
            UpdateConnState();
        }

        void ScanTick()
        {
            RefreshPorts(false);
            var p = port;
            if (p != null)
            {
                bool present = Array.IndexOf(lastNames, p.PortName.ToUpperInvariant()) >= 0;
                bool open;
                try { open = p.IsOpen; } catch { open = false; }
                if (!present || !open) PortLost();
            }
            else if (wantConnected && chkReconnect.Checked && targetPort != null)
            {
                // Espera a porta aparecer por ~2 s antes de tentar (o driver USB precisa de um tempo).
                if (Array.IndexOf(lastNames, targetPort.ToUpperInvariant()) >= 0)
                {
                    if (++seenTicks >= 2) Connect(true);
                }
                else seenTicks = 0;
            }
        }

        void ToggleConnect()
        {
            if (port != null || wantConnected) Disconnect(true);
            else Connect(false);
        }

        void Connect(bool auto)
        {
            string name = auto ? targetPort : SelectedPortName();
            if (name == null)
            {
                AddSys("Nenhuma porta selecionada.");
                return;
            }
            int baud;
            if (!TryGetBaud(out baud))
            {
                AddSys("Baud rate inválido: " + cbBaud.Text);
                return;
            }
            if (!auto && WindowManager.IsOpenElsewhere(name, this))
            {
                AddSys(name + " já está aberta em outra janela.");
                return;
            }

            SerialPort p = null;
            try
            {
                p = new SerialPort(name, baud, GetParity(), GetDataBits(), GetStopBits());
                p.Handshake = GetHandshake();
                p.ReadBufferSize = 1 << 16;
                p.WriteBufferSize = 1 << 15;
                p.ReadTimeout = 500;
                p.WriteTimeout = 2000;
                p.DtrEnable = chkDtr.Checked;
                if (!UsesRtsHandshake(p.Handshake)) p.RtsEnable = chkRts.Checked;
                p.DataReceived += OnDataReceived;
                p.Open();
            }
            catch (Exception ex)
            {
                if (p != null)
                {
                    p.DataReceived -= OnDataReceived;
                    try { p.Dispose(); } catch { }
                }
                seenTicks = 0;
                if (!auto)
                {
                    string why = ex is UnauthorizedAccessException
                        ? "acesso negado (a porta está aberta em outro programa, ex.: Arduino IDE ou upload em andamento?)"
                        : ex.Message;
                    AddSys("Não foi possível abrir " + name + ": " + why);
                }
                UpdateConnState();
                return;
            }

            lock (rxLock) rxQueue.Clear();
            decoder.Reset();
            escState = 0;
            lastWasCR = false;
            connectTime = lastLineTime = DateTime.Now;
            targetPort = name;
            wantConnected = true;
            seenTicks = 0;
            port = p;
            WindowManager.SetOpen(name, this, true);
            AddSys((auto ? "Reconectado a " : "Conectado a ") + name + " @ " + baud + " " + ConfigString());
            UpdateConnState();
        }

        /// <param name="user">true quando o usuário pediu: cancela também a reconexão automática.</param>
        void Disconnect(bool user)
        {
            if (user) wantConnected = false;
            var p = port;
            if (p == null)
            {
                if (user) AddSys("Reconexão cancelada.");
                UpdateConnState();
                return;
            }
            if (!paused) ProcessQueue();
            port = null;
            p.DataReceived -= OnDataReceived;
            try { p.Close(); } catch { }
            WindowManager.SetOpen(p.PortName, this, false);
            AddSys("Desconectado de " + p.PortName);
            UpdateConnState();
        }

        void PortLost()
        {
            var p = port;
            if (p == null) return;
            if (!paused) ProcessQueue();
            port = null;
            p.DataReceived -= OnDataReceived;
            // Fechar uma porta USB removida pode travar; faz em segundo plano.
            ThreadPool.QueueUserWorkItem(delegate { try { p.Close(); } catch { } });
            WindowManager.SetOpen(p.PortName, this, false);
            seenTicks = 0;
            if (!chkReconnect.Checked) wantConnected = false;
            AddSys(p.PortName + " foi desconectada" + (chkReconnect.Checked ? " - aguardando reconexão..." : "."));
            UpdateConnState();
        }

        void ApplyPortParam(string what)
        {
            var p = port;
            if (p == null || loading) return;
            try
            {
                switch (what)
                {
                    case "baud":
                        int b;
                        if (!TryGetBaud(out b)) { AddSys("Baud rate inválido: " + cbBaud.Text); return; }
                        if (p.BaudRate == b) return;
                        p.BaudRate = b;
                        break;
                    case "data": p.DataBits = GetDataBits(); break;
                    case "parity": p.Parity = GetParity(); break;
                    case "stop": p.StopBits = GetStopBits(); break;
                    case "flow":
                        p.Handshake = GetHandshake();
                        if (!UsesRtsHandshake(p.Handshake)) p.RtsEnable = chkRts.Checked;
                        break;
                    case "dtr": p.DtrEnable = chkDtr.Checked; break;
                    case "rts": p.RtsEnable = chkRts.Checked; break;
                }
                AddSys("Parâmetros: " + p.BaudRate + " " + ConfigString() + ", fluxo " + cbFlow.Text +
                       ", DTR " + (chkDtr.Checked ? "1" : "0") + ", RTS " + (chkRts.Checked ? "1" : "0"));
            }
            catch (Exception ex)
            {
                AddSys("Não foi possível alterar: " + ex.Message);
            }
            UpdateConnState();
        }

        void UpdateConnState()
        {
            var p = port;
            bool waiting = p == null && wantConnected && chkReconnect.Checked;
            btnConnect.Text = p != null || waiting ? "Desconectar" : "Conectar";
            if (p != null)
            {
                lblStatus.Text = "Conectado: " + p.PortName + " @ " + p.BaudRate + " " + ConfigString();
                lblStatus.ForeColor = Color.FromArgb(0, 128, 0);
                Text = p.PortName + " @ " + p.BaudRate + " - Monitor Serial";
            }
            else if (waiting)
            {
                lblStatus.Text = "Aguardando " + targetPort + " reaparecer...";
                lblStatus.ForeColor = Color.FromArgb(190, 100, 0);
                Text = targetPort + " (aguardando) - Monitor Serial";
            }
            else
            {
                lblStatus.Text = "Desconectado";
                lblStatus.ForeColor = SystemColors.ControlText;
                string sel = SelectedPortName();
                Text = sel != null ? sel + " - Monitor Serial" : "Monitor Serial";
            }
            if (paused) lblStatus.Text += "    |    PAUSADO";
        }

        bool TryGetBaud(out int baud)
        {
            return int.TryParse(cbBaud.Text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out baud)
                   && baud > 0 && baud <= 20000000;
        }

        int GetDataBits() { return cbData.SelectedIndex + 5; }

        Parity GetParity()
        {
            switch (cbParity.SelectedIndex)
            {
                case 1: return Parity.Even;
                case 2: return Parity.Odd;
                case 3: return Parity.Mark;
                case 4: return Parity.Space;
                default: return Parity.None;
            }
        }

        StopBits GetStopBits()
        {
            switch (cbStop.SelectedIndex)
            {
                case 1: return StopBits.OnePointFive;
                case 2: return StopBits.Two;
                default: return StopBits.One;
            }
        }

        Handshake GetHandshake()
        {
            switch (cbFlow.SelectedIndex)
            {
                case 1: return Handshake.RequestToSend;
                case 2: return Handshake.XOnXOff;
                case 3: return Handshake.RequestToSendXOnXOff;
                default: return Handshake.None;
            }
        }

        static bool UsesRtsHandshake(Handshake h)
        {
            return h == Handshake.RequestToSend || h == Handshake.RequestToSendXOnXOff;
        }

        string ConfigString()
        {
            return GetDataBits() + "NEOMS".Substring(Math.Max(0, cbParity.SelectedIndex), 1) + cbStop.Text;
        }

        Encoding GetEncoding()
        {
            switch (cbEnc.SelectedIndex)
            {
                case 1: return Encoding.ASCII;
                case 2: return Encoding.GetEncoding(1252);
                case 3: return Encoding.GetEncoding(28591);
                default: return new UTF8Encoding(false);
            }
        }

        // =====================================================================
        //  Recepção
        // =====================================================================

        // Roda numa thread do sistema: só lê os bytes e enfileira com o horário de chegada.
        void OnDataReceived(object sender, SerialDataReceivedEventArgs e)
        {
            var p = sender as SerialPort;
            if (p == null || p != port) return;
            try
            {
                int n = p.BytesToRead;
                if (n <= 0) return;
                var buf = new byte[n];
                int r = p.Read(buf, 0, n);
                if (r <= 0) return;
                if (r < n) Array.Resize(ref buf, r);
                var ch = new Chunk { Time = DateTime.Now, Data = buf };
                lock (rxLock) rxQueue.Add(ch);
                Interlocked.Add(ref rxBytes, r);
            }
            catch { }
        }

        void UiTick()
        {
            if (!paused) ProcessQueue();
            if (logWriter != null && logDirty)
            {
                try { logWriter.Flush(); } catch { }
                logDirty = false;
            }
            long rx = Interlocked.Read(ref rxBytes), tx = Interlocked.Read(ref txBytes);
            if (rx != shownRx) { shownRx = rx; lblRx.Text = "RX: " + FormatBytes(rx); }
            if (tx != shownTx) { shownTx = tx; lblTx.Text = "TX: " + FormatBytes(tx); }
            if (rxLines != shownLines) { shownLines = rxLines; lblLines.Text = "Linhas: " + rxLines.ToString("N0"); }
        }

        void ProcessQueue()
        {
            List<Chunk> items;
            lock (rxLock)
            {
                if (rxQueue.Count == 0) return;
                items = rxQueue;
                rxQueue = new List<Chunk>();
            }
            var segs = new SegList();
            foreach (Chunk ch in items)
            {
                if (viewMode == 1) ProcessHex(ch, segs);
                else ProcessText(ch, segs);
            }
            Append(segs);
        }

        void ProcessText(Chunk ch, SegList segs)
        {
            int n = decoder.GetCharCount(ch.Data, 0, ch.Data.Length);
            var buf = new char[n];
            decoder.GetChars(ch.Data, 0, ch.Data.Length, buf, 0);
            bool stripAnsi = chkAnsi.Checked;

            for (int i = 0; i < n; i++)
            {
                char c = buf[i];
                if (escState != 0)
                {
                    if (c == '\r' || c == '\n') escState = 0;      // sequência malformada: não engole a quebra
                    else
                    {
                        if (escState == 1) escState = c == '[' ? 2 : 0;
                        else if (c >= '@' && c <= '~') escState = 0;  // byte final da sequência CSI
                        continue;
                    }
                }
                if (c == '\x1b' && stripAnsi) { escState = 1; continue; }

                // \r\n, \n e \r sozinho contam como uma quebra de linha
                if (c == '\n' && lastWasCR) { lastWasCR = false; continue; }
                lastWasCR = c == '\r';
                if (c == '\r' || c == '\n')
                {
                    if (atLineStart) StartLine(ch.Time, segs);
                    segs.Add(KRx, '\n');
                    atLineStart = true;
                    rxLines++;
                    continue;
                }

                if (atLineStart) StartLine(ch.Time, segs);
                if ((c < ' ' && c != '\t') || c == '\x7f') c = '·';
                segs.Add(KRx, c);
            }
        }

        void ProcessHex(Chunk ch, SegList segs)
        {
            foreach (byte b in ch.Data)
            {
                if (atLineStart) { StartLine(ch.Time, segs); hexCol = 0; }
                segs.Add(KRx, HexTable[b]);
                if (b == 0x0A || ++hexCol >= 16)
                {
                    segs.Add(KRx, '\n');
                    atLineStart = true;
                    rxLines++;
                }
            }
        }

        void StartLine(DateTime t, SegList segs)
        {
            if (tsMode > 0) segs.Add(KTs, FormatTs(t, true) + " -> ");
            else lastLineTime = t;
            atLineStart = false;
        }

        string FormatTs(DateTime t, bool updateDelta)
        {
            string s;
            switch (tsMode)
            {
                case 1: s = t.ToString("HH:mm:ss.fff", CultureInfo.InvariantCulture); break;
                case 2: s = t.ToString("HH:mm:ss", CultureInfo.InvariantCulture); break;
                case 3: s = t.ToString("yyyy-MM-dd HH:mm:ss.fff", CultureInfo.InvariantCulture); break;
                case 4: s = string.Format("{0,10:0.000}s", (t - connectTime).TotalSeconds); break;
                case 5: s = string.Format("+{0,7:0}ms", Math.Max(0, (t - lastLineTime).TotalMilliseconds)); break;
                default: s = ""; break;
            }
            if (updateDelta) lastLineTime = t;
            return s;
        }

        // =====================================================================
        //  Saída
        // =====================================================================

        const int WM_SETREDRAW = 0x000B;
        const int EM_GETSCROLLPOS = 0x0400 + 221;
        const int EM_SETSCROLLPOS = 0x0400 + 222;

        [StructLayout(LayoutKind.Sequential)]
        struct POINT { public int X, Y; }

        [DllImport("user32.dll")]
        static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll")]
        static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, ref POINT lParam);

        Color ColorOf(int kind)
        {
            switch (kind)
            {
                case KTs: return pal.Ts;
                case KSent: return pal.Sent;
                case KSys: return pal.Sys;
                default: return pal.Rx;
            }
        }

        void BreakLine(SegList segs)
        {
            if (atLineStart) return;
            segs.Add(KRx, '\n');
            atLineStart = true;
            hexCol = 0;
        }

        void AddSys(string msg)
        {
            if (!paused) ProcessQueue();   // mantém a ordem com o que já chegou
            var segs = new SegList();
            BreakLine(segs);
            segs.Add(KSys, "-- " + msg + " --\n");
            Append(segs);
        }

        void SafeSys(string msg)
        {
            try
            {
                if (!IsDisposed) BeginInvoke(new Action(delegate { AddSys(msg); }));
            }
            catch { }
        }

        void AddEcho(string text)
        {
            if (!paused) ProcessQueue();
            var segs = new SegList();
            BreakLine(segs);
            if (tsMode > 0) segs.Add(KTs, FormatTs(DateTime.Now, false) + " <- ");
            else segs.Add(KSent, "> ");
            segs.Add(KSent, text + "\n");
            Append(segs);
        }

        void Append(SegList segs)
        {
            if (segs.Count == 0) return;
            if (segs.Count > 600)
            {
                // Muito tráfego: junta tudo num trecho só para não travar a interface.
                var all = new Seg { Kind = KRx };
                foreach (Seg s in segs) all.Text.Append(s.Text);
                segs.Clear();
                segs.Add(all);
            }

            if (logWriter != null)
            {
                try
                {
                    foreach (Seg s in segs) logWriter.Write(s.Text.ToString().Replace("\n", "\r\n"));
                    logDirty = true;
                }
                catch
                {
                    CloseLog();
                    suppressLog = true;
                    chkLog.Checked = false;
                    suppressLog = false;
                }
            }

            IntPtr h = output.Handle;
            bool follow = chkAutoscroll.Checked;
            int selStart = output.SelectionStart, selLen = output.SelectionLength;
            var sp = new POINT();
            if (!follow) SendMessage(h, EM_GETSCROLLPOS, IntPtr.Zero, ref sp);
            SendMessage(h, WM_SETREDRAW, IntPtr.Zero, IntPtr.Zero);
            try
            {
                foreach (Seg s in segs)
                {
                    output.Select(output.TextLength, 0);
                    output.SelectionColor = ColorOf(s.Kind);
                    output.SelectedText = s.Text.ToString();
                }
                int removed = TrimOutput();
                selStart -= removed;
                if (selStart < 0) { selStart = 0; selLen = 0; }
            }
            finally
            {
                SendMessage(h, WM_SETREDRAW, new IntPtr(1), IntPtr.Zero);
            }
            if (follow)
            {
                output.Select(output.TextLength, 0);
                output.ScrollToCaret();
            }
            else
            {
                output.Select(selStart, selLen);
                SendMessage(h, EM_SETSCROLLPOS, IntPtr.Zero, ref sp);
            }
            output.Invalidate();
        }

        /// <summary>Remove linhas antigas quando passa do limite. Retorna quantos caracteres saíram.</summary>
        int TrimOutput()
        {
            int maxLines = (int)numMaxLines.Value;
            int len = output.TextLength;
            int lines = output.GetLineFromCharIndex(len) + 1;
            int cut = 0;
            if (lines > maxLines + Math.Max(50, maxLines / 10))
                cut = output.GetFirstCharIndexFromLine(lines - maxLines);
            if (len - Math.Max(cut, 0) > MaxChars)
            {
                int target = len - MaxChars * 4 / 5;
                int next = output.GetFirstCharIndexFromLine(output.GetLineFromCharIndex(target) + 1);
                cut = next > 0 ? next : target;
            }
            if (cut <= 0) return 0;
            output.Select(0, cut);
            output.SelectedText = "";
            return cut;
        }

        void ClearOutput()
        {
            float z = output.ZoomFactor;
            output.Clear();
            output.ZoomFactor = 1f;
            output.ZoomFactor = z;   // o RichTextBox perde o zoom ao limpar
            Interlocked.Exchange(ref rxBytes, 0);
            Interlocked.Exchange(ref txBytes, 0);
            rxLines = 0;
            atLineStart = true;
            hexCol = 0;
        }

        void TogglePause()
        {
            paused = !paused;
            btnPause.Text = paused ? "Continuar" : "Pausar";
            if (!paused) ProcessQueue();
            UpdateConnState();
        }

        void ApplyTheme(bool dark, bool recolor)
        {
            Palette old = pal;
            pal = dark ? DarkPal : LightPal;
            output.BackColor = pal.Back;
            output.ForeColor = pal.Rx;
            if (!recolor || old == pal || output.TextLength == 0) return;
            try
            {
                // Troca as cores na tabela de cores do RTF, preservando o tipo de cada trecho.
                string rtf = output.Rtf;
                Color[] from = { old.Rx, old.Ts, old.Sent, old.Sys };
                Color[] to = { pal.Rx, pal.Ts, pal.Sent, pal.Sys };
                for (int i = 0; i < from.Length; i++) rtf = rtf.Replace(RtfColor(from[i]), "\u0001" + i + "\u0001");
                for (int i = 0; i < to.Length; i++) rtf = rtf.Replace("\u0001" + i + "\u0001", RtfColor(to[i]));
                float z = output.ZoomFactor;
                output.Rtf = rtf;
                output.ZoomFactor = 1f;
                output.ZoomFactor = z;
                if (chkAutoscroll.Checked) { output.Select(output.TextLength, 0); output.ScrollToCaret(); }
            }
            catch { }
        }

        static string RtfColor(Color c)
        {
            return "\\red" + c.R + "\\green" + c.G + "\\blue" + c.B + ";";
        }

        void FindNext()
        {
            string s = txtFind.Text;
            if (s.Length == 0) { txtFind.Focus(); return; }
            int start = Math.Min(output.SelectionStart + output.SelectionLength, output.TextLength);
            int idx = output.Find(s, start, RichTextBoxFinds.None);
            if (idx < 0 && start > 0) idx = output.Find(s, 0, RichTextBoxFinds.None);
            if (idx < 0)
            {
                System.Media.SystemSounds.Beep.Play();
                lblStatus.Text = "\"" + s + "\" não encontrado";
                return;
            }
            chkAutoscroll.Checked = false;   // senão a próxima linha recebida tira a seleção da tela
            output.ScrollToCaret();
        }

        void SaveOutput()
        {
            using (var dlg = new SaveFileDialog())
            {
                dlg.Filter = "Texto (*.txt)|*.txt|Log (*.log)|*.log|Todos os arquivos|*.*";
                dlg.FileName = DefaultFileName();
                if (dlg.ShowDialog(this) != DialogResult.OK) return;
                try
                {
                    File.WriteAllText(dlg.FileName, output.Text.Replace("\n", "\r\n"), new UTF8Encoding(false));
                    AddSys("Saída salva em " + dlg.FileName);
                }
                catch (Exception ex)
                {
                    AddSys("Erro ao salvar: " + ex.Message);
                }
            }
        }

        string DefaultFileName()
        {
            string p = SelectedPortName() ?? "serial";
            return "serial_" + p + "_" + DateTime.Now.ToString("yyyyMMdd_HHmmss") + ".txt";
        }

        void LogToggled()
        {
            if (suppressLog) return;
            if (!chkLog.Checked)
            {
                if (logWriter != null) AddSys("Gravação de log encerrada.");
                CloseLog();
                return;
            }
            using (var dlg = new SaveFileDialog())
            {
                dlg.Filter = "Log (*.log)|*.log|Texto (*.txt)|*.txt|Todos os arquivos|*.*";
                dlg.FileName = Path.ChangeExtension(DefaultFileName(), ".log");
                dlg.OverwritePrompt = false;
                dlg.Title = "Gravar log em (o conteúdo é acrescentado ao final do arquivo)";
                bool ok = false;
                if (dlg.ShowDialog(this) == DialogResult.OK)
                {
                    try
                    {
                        var fs = new FileStream(dlg.FileName, FileMode.Append, FileAccess.Write, FileShare.Read);
                        logWriter = new StreamWriter(fs, new UTF8Encoding(false));
                        ok = true;
                    }
                    catch (Exception ex)
                    {
                        AddSys("Não foi possível abrir o log: " + ex.Message);
                    }
                }
                if (ok) AddSys("Gravando log em " + dlg.FileName);
                else
                {
                    suppressLog = true;
                    chkLog.Checked = false;
                    suppressLog = false;
                }
            }
        }

        void CloseLog()
        {
            var w = logWriter;
            logWriter = null;
            if (w != null) try { w.Dispose(); } catch { }
        }

        static string FormatBytes(long n)
        {
            if (n < 10000) return n + " B";
            if (n < 10000000) return (n / 1024.0).ToString("0.0") + " KB";
            return (n / 1048576.0).ToString("0.0") + " MB";
        }

        static string[] BuildHexTable()
        {
            var t = new string[256];
            for (int i = 0; i < 256; i++) t[i] = i.ToString("X2") + " ";
            return t;
        }

        // =====================================================================
        //  Envio
        // =====================================================================

        void DoSend(bool fromRepeat)
        {
            string text = cbSend.Text;
            var p = port;
            if (p == null)
            {
                if (!fromRepeat) AddSys("Não conectado. Selecione a porta e clique em Conectar (F5).");
                return;
            }

            byte[] data;
            string echo;
            if (chkHex.Checked)
            {
                string err;
                data = ParseHex(text, out err);
                if (data == null)
                {
                    AddSys("HEX inválido: " + err);
                    if (fromRepeat) chkRepeat.Checked = false;
                    return;
                }
                echo = BytesToHex(data);
            }
            else
            {
                data = EncodeText(text, LineEndValues[Math.Max(0, cbLineEnd.SelectedIndex)]);
                echo = text;
            }
            if (data.Length == 0) return;

            try
            {
                p.Write(data, 0, data.Length);
                Interlocked.Add(ref txBytes, data.Length);
            }
            catch (Exception ex)
            {
                AddSys("Erro ao enviar: " + ex.Message);
                return;
            }

            if (chkEcho.Checked) AddEcho(echo);
            if (!fromRepeat)
            {
                Settings.AddHistory(text);
                histIndex = -1;
                if (!chkRepeat.Checked) cbSend.Text = "";
            }
        }

        byte[] EncodeText(string text, string lineEnd)
        {
            if (!chkEsc.Checked) return encoding.GetBytes(text + lineEnd);

            var ms = new MemoryStream();
            var pending = new StringBuilder();
            for (int i = 0; i < text.Length; i++)
            {
                char c = text[i];
                if (c != '\\' || i + 1 >= text.Length) { pending.Append(c); continue; }
                char n = text[++i];
                switch (n)
                {
                    case 'n': pending.Append('\n'); break;
                    case 'r': pending.Append('\r'); break;
                    case 't': pending.Append('\t'); break;
                    case '0': pending.Append('\0'); break;
                    case '\\': pending.Append('\\'); break;
                    case 'x':
                        if (i + 2 < text.Length && IsHex(text[i + 1]) && IsHex(text[i + 2]))
                        {
                            // \xHH vira um byte cru, sem passar pela codificação de texto
                            WriteText(ms, pending);
                            ms.WriteByte(Convert.ToByte(text.Substring(i + 1, 2), 16));
                            i += 2;
                        }
                        else pending.Append("\\x");
                        break;
                    default: pending.Append('\\').Append(n); break;
                }
            }
            pending.Append(lineEnd);
            WriteText(ms, pending);
            return ms.ToArray();
        }

        void WriteText(MemoryStream ms, StringBuilder sb)
        {
            if (sb.Length == 0) return;
            byte[] b = encoding.GetBytes(sb.ToString());
            ms.Write(b, 0, b.Length);
            sb.Length = 0;
        }

        static bool IsHex(char c)
        {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        }

        static byte[] ParseHex(string s, out string err)
        {
            err = null;
            var list = new List<byte>();
            foreach (string tok0 in s.Split(new[] { ' ', ',', ';', '\t', '-', ':' }, StringSplitOptions.RemoveEmptyEntries))
            {
                string tok = tok0;
                if (tok.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) tok = tok.Substring(2);
                if (tok.Length % 2 == 1) tok = "0" + tok;
                for (int i = 0; i < tok.Length; i += 2)
                {
                    if (!IsHex(tok[i]) || !IsHex(tok[i + 1]))
                    {
                        err = "\"" + tok0 + "\"";
                        return null;
                    }
                    list.Add(Convert.ToByte(tok.Substring(i, 2), 16));
                }
            }
            return list.ToArray();
        }

        static string BytesToHex(byte[] data)
        {
            var sb = new StringBuilder(data.Length * 3);
            foreach (byte b in data) sb.Append(HexTable[b]);
            return sb.ToString().TrimEnd();
        }

        void HistoryNav(int dir)
        {
            var h = Settings.History;
            if (h.Count == 0) return;
            if (histIndex == -1 && dir > 0) histDraft = cbSend.Text;
            int ni = Math.Max(-1, Math.Min(h.Count - 1, histIndex + dir));
            if (ni == histIndex) return;
            histIndex = ni;
            cbSend.Text = ni == -1 ? histDraft : h[ni];
            cbSend.SelectionStart = cbSend.Text.Length;
        }

        void SendFile()
        {
            var p = port;
            if (p == null)
            {
                AddSys("Não conectado.");
                return;
            }
            string path;
            using (var dlg = new OpenFileDialog())
            {
                dlg.Title = "Enviar arquivo (conteúdo bruto)";
                if (dlg.ShowDialog(this) != DialogResult.OK) return;
                path = dlg.FileName;
            }
            byte[] data;
            try { data = File.ReadAllBytes(path); }
            catch (Exception ex)
            {
                AddSys("Erro ao ler arquivo: " + ex.Message);
                return;
            }
            AddSys("Enviando " + Path.GetFileName(path) + " (" + FormatBytes(data.Length) + ")...");
            ThreadPool.QueueUserWorkItem(delegate
            {
                try
                {
                    for (int off = 0; off < data.Length; off += 1024)
                    {
                        int n = Math.Min(1024, data.Length - off);
                        p.Write(data, off, n);
                        Interlocked.Add(ref txBytes, n);
                    }
                    SafeSys("Arquivo enviado: " + Path.GetFileName(path));
                }
                catch (Exception ex)
                {
                    SafeSys("Erro ao enviar arquivo: " + ex.Message);
                }
            });
        }
    }
}
