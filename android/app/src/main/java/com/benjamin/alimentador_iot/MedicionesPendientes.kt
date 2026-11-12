package com.benjamin.alimentador_iot

import android.content.ContentValues
import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper
import java.util.UUID

// datos locales pendientes separados por usuario
class MedicionesPendientes(context: Context) : SQLiteOpenHelper(
    context.applicationContext, "mediciones_pendientes.db", null, 1
) {
    data class Medicion(
        val id: String,
        val uid: String,
        val nivel: Int,
        val raciones: Int,
        val maxRaciones: Int,
        val fechaLocal: Long
    )

    override fun onCreate(db: SQLiteDatabase) {
        db.execSQL("""
            CREATE TABLE pendientes (
                id TEXT PRIMARY KEY NOT NULL,
                uid TEXT NOT NULL,
                nivel INTEGER NOT NULL,
                raciones INTEGER NOT NULL,
                max_raciones INTEGER NOT NULL,
                fecha_local INTEGER NOT NULL
            )
        """.trimIndent())
        db.execSQL("CREATE INDEX idx_pendientes_uid_fecha ON pendientes(uid, fecha_local)")
    }

    override fun onUpgrade(db: SQLiteDatabase, oldVersion: Int, newVersion: Int) {
        throw IllegalStateException("Migración SQLite pendiente: $oldVersion -> $newVersion")
    }

    fun agregar(uid: String, nivel: Int, raciones: Int, maxRaciones: Int): String {
        require(uid.isNotBlank() && nivel in 0..100 && raciones >= 0 && maxRaciones > 0)
        val id = UUID.randomUUID().toString()
        val valores = ContentValues().apply {
            put("id", id)
            put("uid", uid)
            put("nivel", nivel)
            put("raciones", raciones)
            put("max_raciones", maxRaciones)
            put("fecha_local", System.currentTimeMillis())
        }
        check(writableDatabase.insertOrThrow("pendientes", null, valores) != -1L)
        return id
    }

    fun obtener(uid: String, limite: Int = 100): List<Medicion> {
        val resultado = mutableListOf<Medicion>()
        readableDatabase.query(
            "pendientes",
            arrayOf("id", "uid", "nivel", "raciones", "max_raciones", "fecha_local"),
            "uid = ?", arrayOf(uid), null, null, "fecha_local ASC", limite.coerceIn(1, 100).toString()
        ).use { cursor ->
            while (cursor.moveToNext()) {
                resultado.add(Medicion(
                    cursor.getString(0), cursor.getString(1), cursor.getInt(2),
                    cursor.getInt(3), cursor.getInt(4), cursor.getLong(5)
                ))
            }
        }
        return resultado
    }

    fun eliminar(id: String, uid: String): Boolean =
        writableDatabase.delete("pendientes", "id = ? AND uid = ?", arrayOf(id, uid)) == 1

    fun cantidad(uid: String): Int {
        readableDatabase.rawQuery(
            "SELECT COUNT(*) FROM pendientes WHERE uid = ?", arrayOf(uid)
        ).use { cursor ->
            return if (cursor.moveToFirst()) cursor.getInt(0) else 0
        }
    }
}
