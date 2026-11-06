// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.ds

import android.os.Bundle
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import androidx.appcompat.app.AppCompatActivity
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import org.citra.citra_emu.R
import org.citra.citra_emu.utils.ThemeUtil

/**
 * Settings of the DS core: the same options as the desktop frontend's
 * `DS::ConfigDialog`, backed by the shared preferences keys in [DsSettings].
 *
 * BIOS, firmware and NAND dumps are entered as paths; the DS core reads them with
 * stdio, so they have to be real files (anywhere the app can read).
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
        override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
            setPreferencesFromResource(R.xml.ds_preferences, rootKey)
            // Paths are free text; show what is set as the summary.
            for (key in PATH_KEYS) {
                findPreference<Preference>(key)?.let { preference ->
                    preference.summaryProvider =
                        Preference.SummaryProvider<Preference> { pref ->
                            val value = pref.sharedPreferences?.getString(pref.key, "")
                            if (value.isNullOrEmpty()) {
                                getString(R.string.ds_path_not_set)
                            } else {
                                value
                            }
                        }
                }
            }
        }

        override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
            super.onViewCreated(view, savedInstanceState)
            view.fitsSystemWindows = true
        }
    }

    companion object {
        private val PATH_KEYS = listOf(
            DsSettings.KEY_DS_BIOS9,
            DsSettings.KEY_DS_BIOS7,
            DsSettings.KEY_DS_FIRMWARE,
            DsSettings.KEY_DSI_BIOS9,
            DsSettings.KEY_DSI_BIOS7,
            DsSettings.KEY_DSI_FIRMWARE,
            DsSettings.KEY_DSI_NAND,
            DsSettings.KEY_DLDI_SYNC_FOLDER
        )
    }
}
