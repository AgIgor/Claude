package com.example.espio

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothSocket
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.Gravity
import android.view.View
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.Spinner
import android.widget.Switch
import android.widget.TextView
import java.io.IOException
import java.io.OutputStream
import java.util.UUID

// UUID padrão do perfil Serial Port (SPP), o mesmo usado pelo BluetoothSerial do ESP32
private val SPP_UUID: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")
private const val N_OUT = 4
private const val N_IN = 4
private const val REQ_BT = 1

@SuppressLint("MissingPermission", "SetTextI18n", "UseSwitchCompatOrMaterialCode")
class MainActivity : Activity() {

    private val ui = Handler(Looper.getMainLooper())
    private var adapter: BluetoothAdapter? = null
    private var devices: List<BluetoothDevice> = emptyList()

    @Volatile private var socket: BluetoothSocket? = null
    private var out: OutputStream? = null

    private lateinit var spinner: Spinner
    private lateinit var btnConnect: Button
    private lateinit var txtStatus: TextView
    private val outSwitches = mutableListOf<Switch>()
    private val inLeds = mutableListOf<View>()
    private lateinit var txtAnalog: TextView
    private lateinit var barAnalog: ProgressBar
    private var updatingUi = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        installCrashLogger()
        adapter = getSystemService(BluetoothManager::class.java)?.adapter
        buildUi()
        setConnectedUi(false)
        if (hasBtPermission()) loadPairedDevices() else requestBtPermission()
        showLastCrash()
    }

    // ---------- Diagnóstico: guarda o erro de um crash e mostra na próxima abertura ----------

    private fun installCrashLogger() {
        val prefs = getSharedPreferences("crash", MODE_PRIVATE)
        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { t, e ->
            prefs.edit().putString("last", Log.getStackTraceString(e).take(1500)).commit()
            previous?.uncaughtException(t, e)
        }
    }

    private fun showLastCrash() {
        val prefs = getSharedPreferences("crash", MODE_PRIVATE)
        val crash = prefs.getString("last", null) ?: return
        prefs.edit().remove("last").apply()
        AlertDialog.Builder(this)
            .setTitle("O app fechou na última vez")
            .setMessage(crash)
            .setPositiveButton("OK", null)
            .show()
    }

    override fun onDestroy() {
        disconnect()
        super.onDestroy()
    }

    // ---------- Permissões / dispositivos ----------

    private fun hasBtPermission() =
        Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
            checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED

    private fun requestBtPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            requestPermissions(arrayOf(Manifest.permission.BLUETOOTH_CONNECT), REQ_BT)
        }
    }

    override fun onRequestPermissionsResult(code: Int, perms: Array<out String>, results: IntArray) {
        super.onRequestPermissionsResult(code, perms, results)
        if (code == REQ_BT && hasBtPermission()) loadPairedDevices()
        else txtStatus.text = "Permissão de Bluetooth negada"
    }

    private fun loadPairedDevices() {
        val bt = adapter
        if (bt == null) { txtStatus.text = "Este aparelho não tem Bluetooth"; return }
        if (!bt.isEnabled) { txtStatus.text = "Ligue o Bluetooth e toque em Atualizar"; return }
        devices = bt.bondedDevices.sortedByDescending { it.name?.startsWith("ESP32") == true }
        spinner.adapter = ArrayAdapter(
            this, android.R.layout.simple_spinner_dropdown_item,
            devices.map { "${it.name ?: "?"}  (${it.address})" }
        )
        txtStatus.text = if (devices.isEmpty())
            "Nenhum dispositivo pareado. Pareie o ESP32 nas configurações do Android."
        else "Selecione o ESP32 e toque em Conectar"
    }

    // ---------- Conexão ----------

    private fun connect() {
        val device = devices.getOrNull(spinner.selectedItemPosition) ?: return
        txtStatus.text = "Conectando a ${device.name}..."
        btnConnect.isEnabled = false
        Thread {
            try {
                val s = openSocket(device)
                socket = s
                out = s.outputStream
                ui.post {
                    txtStatus.text = "Conectado a ${device.name}"
                    setConnectedUi(true)
                }
                send("?")
                readLoop(s)
            } catch (e: Exception) {
                ui.post {
                    txtStatus.text = "Falha ao conectar: ${e.javaClass.simpleName}: ${e.message}"
                    setConnectedUi(false)
                }
            }
        }.start()
    }

    /** Tenta socket seguro e, se falhar, o inseguro (alguns celulares só conectam assim). */
    private fun openSocket(device: BluetoothDevice): BluetoothSocket {
        val secure = device.createRfcommSocketToServiceRecord(SPP_UUID)
        try {
            secure.connect()
            return secure
        } catch (e: IOException) {
            try { secure.close() } catch (_: IOException) {}
        }
        val insecure = device.createInsecureRfcommSocketToServiceRecord(SPP_UUID)
        try {
            insecure.connect()
            return insecure
        } catch (e: IOException) {
            try { insecure.close() } catch (_: IOException) {}
            throw e
        }
    }

    private fun readLoop(s: BluetoothSocket) {
        val reader = s.inputStream.bufferedReader()
        try {
            while (true) {
                val line = reader.readLine() ?: break
                ui.post { handleLine(line.trim()) }
            }
        } catch (_: IOException) {
        }
        if (socket === s) ui.post {
            disconnect()
            txtStatus.text = "Conexão perdida"
        }
    }

    private fun disconnect() {
        val s = socket
        socket = null
        out = null
        try { s?.close() } catch (_: IOException) {}
        setConnectedUi(false)
    }

    private fun send(cmd: String) {
        val o = out ?: return
        Thread {
            try { o.write("$cmd\n".toByteArray()); o.flush() } catch (_: IOException) {}
        }.start()
    }

    // ---------- Protocolo: "S,<saidas>,<entradas>,<analogico>" ----------

    private fun handleLine(line: String) {
        val p = line.split(',')
        if (p.size != 4 || p[0] != "S") return
        updatingUi = true
        p[1].forEachIndexed { i, c -> outSwitches.getOrNull(i)?.isChecked = c == '1' }
        updatingUi = false
        p[2].forEachIndexed { i, c ->
            inLeds.getOrNull(i)?.let { setLed(it, c == '1') }
        }
        p[3].toIntOrNull()?.let {
            txtAnalog.text = "Analógico (GPIO34): $it"
            barAnalog.progress = it
        }
    }

    // ---------- Interface ----------

    private fun buildUi() {
        val dp = resources.displayMetrics.density
        val pad = (16 * dp).toInt()
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
        }

        fun title(t: String) = TextView(this).apply {
            text = t
            textSize = 18f
            setPadding(0, pad, 0, pad / 2)
        }

        // Conexão
        root.addView(title("Conexão"))
        spinner = Spinner(this)
        root.addView(spinner)
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        btnConnect = Button(this).apply {
            setOnClickListener { if (socket == null) connect() else disconnect() }
        }
        val btnRefresh = Button(this).apply {
            text = "Atualizar"
            setOnClickListener { if (hasBtPermission()) loadPairedDevices() else requestBtPermission() }
        }
        row.addView(btnConnect, LinearLayout.LayoutParams(0, -2, 1f))
        row.addView(btnRefresh, LinearLayout.LayoutParams(0, -2, 1f))
        root.addView(row)
        txtStatus = TextView(this)
        root.addView(txtStatus)

        // Saídas
        root.addView(title("Saídas"))
        val outNames = listOf("Saída 1 (GPIO2 / LED)", "Saída 2 (GPIO4)", "Saída 3 (GPIO16)", "Saída 4 (GPIO17)")
        for (i in 0 until N_OUT) {
            val sw = Switch(this).apply {
                text = outNames[i]
                textSize = 16f
                setPadding(0, pad / 2, 0, pad / 2)
                setOnCheckedChangeListener { _, on ->
                    if (!updatingUi) send("O${i + 1}${if (on) 1 else 0}")
                }
            }
            outSwitches += sw
            root.addView(sw)
        }

        // Entradas
        root.addView(title("Entradas"))
        val inNames = listOf("Entrada 1 (GPIO13)", "Entrada 2 (GPIO14)", "Entrada 3 (GPIO27)", "Entrada 4 (GPIO26)")
        for (i in 0 until N_IN) {
            val r = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                setPadding(0, pad / 3, 0, pad / 3)
            }
            val led = View(this)
            setLed(led, null)
            inLeds += led
            r.addView(led, LinearLayout.LayoutParams((20 * dp).toInt(), (20 * dp).toInt()).apply {
                marginEnd = pad
            })
            r.addView(TextView(this).apply { text = inNames[i]; textSize = 16f })
            root.addView(r)
        }
        txtAnalog = TextView(this).apply {
            text = "Analógico (GPIO34): --"
            textSize = 16f
            setPadding(0, pad / 2, 0, 0)
        }
        root.addView(txtAnalog)
        barAnalog = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply { max = 4095 }
        root.addView(barAnalog)

        setContentView(ScrollView(this).apply { addView(root) })
    }

    /** Verde = nível alto (1), vermelho = nível baixo (0), cinza = desconhecido. */
    private fun setLed(v: View, high: Boolean?) {
        v.background = GradientDrawable().apply {
            shape = GradientDrawable.OVAL
            setColor(
                when (high) {
                    true -> Color.rgb(0x2E, 0xB8, 0x4B)
                    false -> Color.rgb(0xD6, 0x3A, 0x3A)
                    null -> Color.LTGRAY
                }
            )
        }
    }

    private fun setConnectedUi(connected: Boolean) {
        btnConnect.isEnabled = true
        btnConnect.text = if (connected) "Desconectar" else "Conectar"
        spinner.isEnabled = !connected
        outSwitches.forEach { it.isEnabled = connected }
        if (!connected) {
            inLeds.forEach { setLed(it, null) }
            txtAnalog.text = "Analógico (GPIO34): --"
            barAnalog.progress = 0
        }
    }
}
