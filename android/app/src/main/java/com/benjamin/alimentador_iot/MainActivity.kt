package com.benjamin.alimentador_iot

import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView
import androidx.activity.enableEdgeToEdge
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import com.google.firebase.auth.FirebaseAuth
import org.eclipse.paho.client.mqttv3.*
import org.eclipse.paho.client.mqttv3.persist.MemoryPersistence
import java.util.UUID
import android.os.SystemClock
import com.google.firebase.database.*
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import android.os.Handler
import android.os.Looper

class MainActivity : AppCompatActivity() {

    private lateinit var auth: FirebaseAuth
    private lateinit var login: LinearLayout
    private lateinit var panel: LinearLayout
    private lateinit var correo: EditText
    private lateinit var clave: EditText
    private lateinit var mensaje: TextView
    private lateinit var usuario: TextView
    private lateinit var entrar: Button

    private lateinit var conexion: TextView
    private lateinit var nivel: TextView
    private lateinit var raciones: TextView
    private lateinit var trama: TextView

    private var mqtt: MqttAsyncClient? = null

    private val broker =
        "ssl://a076afc0.ala.us-east-1.emqxsl.com:8883"
    private val temaDatos = "inacap/ti3042/bs9c4d1e/tel"

    private val temaOrden = "inacap/ti3042/bs9c4d1e/cmd"
    private val temaAck = "inacap/ti3042/bs9c4d1e/ack"

    private val origenOrden = "APP-${UUID.randomUUID()}"
    private var secuenciaOrden = 0L
    private var ordenPendiente: Long? = null

    private lateinit var dispensar: Button
    private lateinit var estadoOrden: TextView
    private val temporizador = Handler(Looper.getMainLooper())

    private val vencimientoOrden = Runnable {
        if (ordenPendiente != null) {
            ordenPendiente = null
            dispensar.isEnabled = true
            estadoOrden.text =
                "Sin confirmación del nodo A. Revisa las raciones " +
                        "antes de volver a enviar."
        }
    }

    private lateinit var base: FirebaseDatabase
    private lateinit var estadoBase: TextView
    private lateinit var historial: TextView

    private var consultaHistorial: Query? = null
    private var listenerHistorial: ValueEventListener? = null
    private var ultimoGuardado = 0L
    private var guardando = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContentView(R.layout.activity_main)

        ViewCompat.setOnApplyWindowInsetsListener(
            findViewById(R.id.main)
        ) { view, insets ->
            val barras = insets.getInsets(
                WindowInsetsCompat.Type.systemBars()
            )
            view.setPadding(
                barras.left, barras.top,
                barras.right, barras.bottom
            )
            insets
        }

        auth = FirebaseAuth.getInstance()

        login = findViewById(R.id.layoutLogin)
        panel = findViewById(R.id.layoutPanel)
        correo = findViewById(R.id.etCorreo)
        clave = findViewById(R.id.etClave)
        mensaje = findViewById(R.id.tvMensaje)
        usuario = findViewById(R.id.tvUsuario)
        entrar = findViewById(R.id.btnEntrar)

        conexion = findViewById(R.id.tvConexion)
        nivel = findViewById(R.id.tvNivel)
        raciones = findViewById(R.id.tvRaciones)
        trama = findViewById(R.id.tvTrama)
        estadoBase = findViewById(R.id.tvBaseDatos)
        historial = findViewById(R.id.tvHistorial)
        dispensar = findViewById(R.id.btnDispensar)
        estadoOrden = findViewById(R.id.tvOrden)

        dispensar.setOnClickListener {
            enviarDispensacion()
        }

        base = FirebaseDatabase.getInstance(
            "https://alimentadoriot-fb433-default-rtdb.firebaseio.com/"
        )

        entrar.setOnClickListener {
            iniciarSesion()
        }

        findViewById<Button>(R.id.btnSalir).setOnClickListener {
            desconectarMqtt()
            detenerHistorial()
            historial.text = ""
            estadoBase.text = "Base de datos: esperando"
            ultimoGuardado = 0L
            temporizador.removeCallbacks(vencimientoOrden)
            ordenPendiente = null
            dispensar.isEnabled = true
            estadoOrden.text = "Sin órdenes enviadas"
            auth.signOut()
            clave.text.clear()
            mensaje.text = ""
            actualizarPantalla()
        }

        actualizarPantalla()
    }

    private fun iniciarSesion() {
        val email = correo.text.toString().trim()
        val password = clave.text.toString()

        if (email.isBlank() || password.isBlank()) {
            mensaje.text = "Ingresa correo y contraseña."
            return
        }

        entrar.isEnabled = false
        mensaje.text = "Iniciando sesión..."

        auth.signInWithEmailAndPassword(email, password)
            .addOnCompleteListener(this) { resultado ->
                entrar.isEnabled = true

                if (resultado.isSuccessful) {
                    clave.text.clear()
                    mensaje.text = ""
                    actualizarPantalla()
                } else {
                    mensaje.text =
                        "No se pudo iniciar sesión. Revisa tus " +
                                "credenciales y la conexión a Internet."
                }
            }
    }

    private fun actualizarPantalla() {
        val cuenta = auth.currentUser

        if (cuenta == null) {
            login.visibility = View.VISIBLE
            panel.visibility = View.GONE
            usuario.text = ""
        } else {
            login.visibility = View.GONE
            panel.visibility = View.VISIBLE
            usuario.text = "Sesión: ${cuenta.email.orEmpty()}"
            conectarMqtt()
            escucharHistorial()
        }
    }

    private fun mostrarSiActivo(
        cliente: MqttAsyncClient,
        accion: () -> Unit
    ) {
        runOnUiThread {
            if (!isDestroyed && mqtt === cliente &&
                auth.currentUser != null
            ) {
                accion()
            }
        }
    }

    private fun conectarMqtt() {
        if (mqtt != null) return

        conexion.text = "Conectando a MQTT..."
        nivel.text = "Nivel de alimento: esperando datos"
        raciones.text = "Raciones: esperando datos"
        trama.text = "Todavía no se recibió una medición"

        try {
            val cliente = MqttAsyncClient(
                broker,
                "android-${UUID.randomUUID()}",
                MemoryPersistence()
            )
            mqtt = cliente

            cliente.setCallback(object : MqttCallbackExtended {

                override fun connectComplete(
                    reconnect: Boolean,
                    serverURI: String?
                ) {
                    if (mqtt !== cliente) return

                    try {
                        cliente.subscribe(
                            arrayOf(temaDatos, temaAck),
                            intArrayOf(0, 0),
                            null,
                            object : IMqttActionListener {
                                override fun onSuccess(
                                    token: IMqttToken?
                                ) {
                                    mostrarSiActivo(cliente) {
                                        conexion.text =
                                            "MQTT conectado. Esperando al nodo A..."
                                    }
                                }

                                override fun onFailure(
                                    token: IMqttToken?,
                                    error: Throwable?
                                ) {
                                    mostrarSiActivo(cliente) {
                                        conexion.text =
                                            "No se pudo suscribir: ${error?.message}"
                                    }
                                }
                            }
                        )
                    } catch (error: Exception) {
                        mostrarSiActivo(cliente) {
                            conexion.text =
                                "Error de suscripción: ${error.message}"
                        }
                    }
                }

                override fun connectionLost(cause: Throwable?) {
                    mostrarSiActivo(cliente) {
                        conexion.text =
                            "MQTT desconectado. Reconectando..."
                        nivel.text = "Nivel de alimento: sin datos actuales"
                        raciones.text = "Raciones: sin datos actuales"
                    }
                }

                override fun messageArrived(
                    topic: String?,
                    message: MqttMessage?
                ) {
                    if (message == null) return

                    val texto = String(message.payload, Charsets.UTF_8)
                    val campos = texto.split(",")

                    if (topic == temaAck) {
                        if (campos.size != 4 || campos[0] != "A") return

                        val numero = campos[1].toLongOrNull() ?: return
                        val resultado = campos[2]
                        val origen = campos[3]

                        mostrarSiActivo(cliente) {
                            if (origen == origenOrden && numero == ordenPendiente) {
                                temporizador.removeCallbacks(vencimientoOrden)
                                ordenPendiente = null
                                dispensar.isEnabled = true

                                estadoOrden.text = when (resultado) {
                                    "OK:DISP" ->
                                        "Ración dispensada y confirmada."
                                    "REJ:LIMITE_DIARIO" ->
                                        "Rechazada: límite de raciones alcanzado."
                                    "REJ:LIMITE_TASA" ->
                                        "Rechazada: espera 6 segundos entre raciones."
                                    "REJ:TOLVA_VACIA" ->
                                        "Rechazada: alimento insuficiente."
                                    else ->
                                        "Respuesta del nodo A: $resultado"
                                }
                            }
                        }
                        return
                    }

                    if (topic != temaDatos) return

                    if (campos.size != 5 || campos[0] != "A") return
                    val secuencia = campos[1].toLongOrNull() ?: return
                    val porcentaje = campos[2].toIntOrNull() ?: return
                    val cantidad = campos[3].toIntOrNull() ?: return
                    val maximo = campos[4].toIntOrNull() ?: return

                    if (secuencia < 0 || porcentaje !in 0..100 ||
                        cantidad < 0 || maximo <= 0
                    ) return

                    mostrarSiActivo(cliente) {
                        conexion.text = "Recibiendo datos del nodo A"
                        nivel.text = "Nivel de alimento: $porcentaje %"
                        raciones.text = "Raciones: $cantidad / $maximo"
                        trama.text = "Última trama: $texto"
                        guardarMedicion(porcentaje, cantidad, maximo)
                    }
                }

                override fun deliveryComplete(
                    token: IMqttDeliveryToken?
                ) {
                    // En esta etapa solamente recibimos datos.
                }
            })

            val opciones = MqttConnectOptions().apply {
                isAutomaticReconnect = true
                isCleanSession = true
                connectionTimeout = 15
                keepAliveInterval = 30

                userName = "android"
                password = "coloque la contra aqui".toCharArray()

                isHttpsHostnameVerificationEnabled = true
            }

            cliente.connect(
                opciones, null,
                object : IMqttActionListener {
                    override fun onSuccess(token: IMqttToken?) {
                        // connectComplete realiza la suscripción.
                    }

                    override fun onFailure(
                        token: IMqttToken?,
                        error: Throwable?
                    ) {
                        mostrarSiActivo(cliente) {
                            conexion.text =
                                "Error MQTT: ${error?.message}. " +
                                        "Cierra sesión y vuelve a entrar."
                        }
                    }
                }
            )
        } catch (error: Exception) {
            conexion.text = "Error MQTT: ${error.message}"
            desconectarMqtt()
        }
    }

    private fun desconectarMqtt() {
        val cliente = mqtt
        mqtt = null

        // La desconexión se realiza fuera del hilo de la pantalla.
        Thread {
            try {
                cliente?.disconnectForcibly()
            } catch (_: Exception) {
            }
            try {
                cliente?.close(true)
            } catch (_: Exception) {
            }
        }.start()
    }

    private fun guardarMedicion(
        porcentaje: Int,
        cantidad: Int,
        maximo: Int
    ) {
        val uid = auth.currentUser?.uid ?: return
        val ahora = SystemClock.elapsedRealtime()

        // Guarda una medición cada 10 segundos por dispositivo.
        if (guardando) return
        if (ultimoGuardado != 0L && ahora - ultimoGuardado < 10_000L) return

        guardando = true
        ultimoGuardado = ahora
        estadoBase.text = "Guardando medición..."

        val registro = mapOf<String, Any>(
            "nivel" to porcentaje,
            "raciones" to cantidad,
            "maxRaciones" to maximo,
            "fecha" to ServerValue.TIMESTAMP
        )

        base.getReference("usuarios")
            .child(uid)
            .child("mediciones")
            .push()
            .setValue(registro)
            .addOnCompleteListener(this) { resultado ->
                guardando = false

                if (auth.currentUser?.uid == uid) {
                    estadoBase.text = if (resultado.isSuccessful) {
                        "Medición guardada en Firebase"
                    } else {
                        "Error al guardar: ${resultado.exception?.message}"
                    }
                }
            }
    }

    private fun escucharHistorial() {
        detenerHistorial()

        val uid = auth.currentUser?.uid ?: return
        historial.text = "Cargando historial..."

        val consulta = base.getReference("usuarios")
            .child(uid)
            .child("mediciones")
            .orderByChild("fecha")
            .limitToLast(10)

        val listener = object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                if (isDestroyed || auth.currentUser?.uid != uid) return

                val formato = SimpleDateFormat(
                    "dd/MM HH:mm:ss",
                    Locale.getDefault()
                )

                val lineas = snapshot.children.map { registro ->
                    val fecha = registro.child("fecha")
                        .getValue(Long::class.java)
                    val nivel = registro.child("nivel")
                        .getValue(Long::class.java)
                    val cantidad = registro.child("raciones")
                        .getValue(Long::class.java)
                    val maximo = registro.child("maxRaciones")
                        .getValue(Long::class.java)

                    val hora = if (fecha != null) {
                        formato.format(Date(fecha))
                    } else {
                        "Fecha pendiente"
                    }

                    "$hora — Nivel: $nivel % — Raciones: $cantidad/$maximo"
                }.reversed()

                historial.text = if (lineas.isEmpty()) {
                    "Sin mediciones guardadas"
                } else {
                    lineas.joinToString("\n\n")
                }
            }

            override fun onCancelled(error: DatabaseError) {
                if (!isDestroyed && auth.currentUser?.uid == uid) {
                    historial.text = "Error de consulta: ${error.message}"
                }
            }
        }

        consultaHistorial = consulta
        listenerHistorial = listener
        consulta.addValueEventListener(listener)
    }

    private fun detenerHistorial() {
        val listener = listenerHistorial
        if (listener != null) {
            consultaHistorial?.removeEventListener(listener)
        }
        consultaHistorial = null
        listenerHistorial = null
    }

    private fun enviarDispensacion() {
        if (auth.currentUser == null || ordenPendiente != null) return

        val cliente = mqtt
        if (cliente == null || !cliente.isConnected) {
            estadoOrden.text = "No hay conexión MQTT."
            return
        }

        secuenciaOrden++
        val numero = secuenciaOrden
        val texto = "$origenOrden,$numero,DISP"

        ordenPendiente = numero
        dispensar.isEnabled = false
        estadoOrden.text = "Enviando orden..."
        temporizador.postDelayed(vencimientoOrden, 8_000L)

        try {
            cliente.publish(
                temaOrden,
                texto.toByteArray(Charsets.UTF_8),
                0,
                false,
                null,
                object : IMqttActionListener {
                    override fun onSuccess(token: IMqttToken?) {
                        mostrarSiActivo(cliente) {
                            if (ordenPendiente == numero) {
                                estadoOrden.text =
                                    "Orden enviada. Esperando confirmación..."
                            }
                        }
                    }

                    override fun onFailure(
                        token: IMqttToken?,
                        error: Throwable?
                    ) {
                        mostrarSiActivo(cliente) {
                            if (ordenPendiente == numero) {
                                temporizador.removeCallbacks(vencimientoOrden)
                                ordenPendiente = null
                                dispensar.isEnabled = true
                                estadoOrden.text =
                                    "No se pudo enviar: ${error?.message}"
                            }
                        }
                    }
                }
            )
        } catch (error: Exception) {
            temporizador.removeCallbacks(vencimientoOrden)
            ordenPendiente = null
            dispensar.isEnabled = true
            estadoOrden.text = "Error al enviar: ${error.message}"
        }
    }

    override fun onDestroy() {
        temporizador.removeCallbacks(vencimientoOrden)
        detenerHistorial()
        desconectarMqtt()
        super.onDestroy()
    }
}