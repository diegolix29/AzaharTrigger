// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.ds

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.format.Formatter
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.PreferenceManager
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import java.io.File
import org.citra.citra_emu.R
import org.citra.citra_emu.dialogs.NetPlayDialog
import org.citra.citra_emu.utils.ThemeUtil

/**
 * Settings of the DS core: the same options as the desktop frontend's
 * `DS::ConfigDialog`, backed by the shared preferences keys in [DsSettings].
 *
 * BIOS, firmware and NAND dumps are picked with the system file picker (one by one
 * or a whole folder) and copied into the DS folder: the core reads them with stdio,
 * which can't open picker documents (see [DsBios]).
 */
class DsSettingsActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        ThemeUtil.setTheme(this)
        super.onCreate(savedInstanceState)

        val container = FrameLayout(this).apply {
            id = R.id.ds_settings_container
            layoutParams = ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
            fitsSystemWindows = true
        }
        setContentView(container)
        title = getString(R.string.ds_settings)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)

        if (savedInstanceState == null) {
            supportFragmentManager.beginTransaction()
                .replace(R.id.ds_settings_container, DsSettingsFragment())
                .commit()
        }
    }

    override fun onSupportNavigateUp(): Boolean {
        finish()
        return true
    }

    class DsSettingsFragment : PreferenceFragmentCompat() {
        private var pendingSlot: DsBios.Slot? = null

        private val pickFile = registerForActivityResult(
            ActivityResultContracts.OpenDocument()
        ) { uri ->
            val slot = pendingSlot ?: return@registerForActivityResult
            pendingSlot = null
            if (uri != null) importFiles(listOf(slot to uri))
        }

        private val pickFolder = registerForActivityResult(
            ActivityResultContracts.OpenDocumentTree()
        ) { tree ->
            if (tree == null) return@registerForActivityResult
            val found = DsBios.matchFolder(requireContext(), tree)
            if (found.isEmpty()) {
                MaterialAlertDialogBuilder(requireContext())
                    .setMessage(R.string.ds_bios_none_found)
                    .setPositiveButton(android.R.string.ok, null)
                    .show()
            } else {
                importFiles(found.map { (slot, doc) -> slot to doc.uri })
            }
        }

        override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
            setPreferencesFromResource(R.xml.ds_preferences, rootKey)
            findPreference<Preference>(KEY_OPEN_LAN_LOBBY)?.setOnPreferenceClickListener {
                NetPlayDialog.open(requireActivity(), dsTab = true)
                true
            }
            findPreference<Preference>(KEY_IMPORT_FOLDER)?.setOnPreferenceClickListener {
                runCatching { pickFolder.launch(null) }
                true
            }
            for (slot in DsBios.SLOTS) {
                findPreference<Preference>(slot.key)?.setOnPreferenceClickListener {
                    onSlotClicked(slot)
                    true
                }
            }
            updateSummaries()
            findPreference<Preference>(DsSettings.KEY_DLDI_SYNC_FOLDER)?.summaryProvider =
                Preference.SummaryProvider<Preference> { pref ->
                    pref.sharedPreferences?.getString(pref.key, "")
                        ?.ifEmpty { null } ?: getString(R.string.ds_path_not_set)
                }
        }

        private fun updateSummaries() {
            val prefs = PreferenceManager.getDefaultSharedPreferences(requireContext())
            for (slot in DsBios.SLOTS) {
                val pref = findPreference<Preference>(slot.key) ?: continue
                val path = prefs.getString(slot.key, "").orEmpty()
                val file = File(path)
                pref.summary = when {
                    path.isEmpty() -> getString(R.string.ds_path_not_set)
                    !file.isFile -> getString(R.string.ds_bios_missing, path)
                    else -> getString(
                        R.string.ds_bios_set_summary,
                        file.name,
                        Formatter.formatShortFileSize(requireContext(), file.length())
                    )
                }
            }
        }

        private fun onSlotClicked(slot: DsBios.Slot) {
            val prefs = PreferenceManager.getDefaultSharedPreferences(requireContext())
            val choose = {
                pendingSlot = slot
                runCatching { pickFile.launch(arrayOf("*/*")) }
                Unit
            }
            val current = prefs.getString(slot.key, "").orEmpty()
            if (current.isEmpty()) {
                choose()
                return
            }
            MaterialAlertDialogBuilder(requireContext())
                .setTitle(findPreference<Preference>(slot.key)?.title)
                .setMessage(current)
                .setPositiveButton(R.string.ds_bios_choose) { _, _ -> choose() }
                .setNeutralButton(R.string.ds_bios_clear) { _, _ ->
                    DsBios.clear(requireContext(), slot)
                    updateSummaries()
                }
                .setNegativeButton(android.R.string.cancel, null)
                .show()
        }

        private fun titleOf(slot: DsBios.Slot): String =
            findPreference<Preference>(slot.key)?.title?.toString() ?: slot.fileName

        /** Copies off the main thread (a DSi NAND is ~240 MB) behind a progress dialog. */
        private fun importFiles(items: List<Pair<DsBios.Slot, Uri>>) {
            val context = requireContext().applicationContext
            val names = items.associate { (slot, _) -> slot to titleOf(slot) }
            val progress = MaterialAlertDialogBuilder(requireContext())
                .setMessage(getString(R.string.ds_bios_copying, names.getValue(items.first().first)))
                .setCancelable(false)
                .show()
            val copying = getString(R.string.ds_bios_copying, "%s")
            val handler = Handler(Looper.getMainLooper())
            Thread({
                val done = mutableListOf<String>()
                val problems = mutableListOf<String>()
                for ((slot, uri) in items) {
                    val name = names.getValue(slot)
                    handler.post { progress.setMessage(copying.replace("%s", name)) }
                    when (val result = DsBios.import(context, slot, uri)) {
                        is DsBios.Result.Copied -> {
                            done.add(name)
                            if (!result.sizeOk) {
                                val file = DsBios.displayName(context, uri) ?: slot.fileName
                                problems.add(context.getString(R.string.ds_bios_wrong_size, file, name))
                            }
                        }
                        is DsBios.Result.Failed ->
                            problems.add(context.getString(R.string.ds_bios_failed, name, result.message))
                    }
                }
                handler.post {
                    runCatching { progress.dismiss() }
                    if (!isAdded) return@post
                    updateSummaries()
                    val lines = mutableListOf<String>()
                    if (done.isNotEmpty()) {
                        lines.add(
                            if (items.size == 1) getString(R.string.ds_bios_copied, done.first())
                            else getString(R.string.ds_bios_imported, done.joinToString(", "))
                        )
                    }
                    lines.addAll(problems)
                    val message = lines.joinToString(System.lineSeparator() + System.lineSeparator())
                    if (problems.isEmpty() && items.size == 1) {
                        Toast.makeText(requireContext(), message, Toast.LENGTH_SHORT).show()
                    } else {
                        MaterialAlertDialogBuilder(requireContext())
                            .setMessage(message)
                            .setPositiveButton(android.R.string.ok, null)
                            .show()
                    }
                }
            }, "DsBiosImport").start()
        }

        override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
            super.onViewCreated(view, savedInstanceState)
            view.fitsSystemWindows = true
        }
    }

    companion object {
        private const val KEY_OPEN_LAN_LOBBY = "ds_open_lan_lobby"
        private const val KEY_IMPORT_FOLDER = "ds_import_bios_folder"

        fun launch(context: Context) {
            context.startActivity(Intent(context, DsSettingsActivity::class.java))
        }

    }
}
