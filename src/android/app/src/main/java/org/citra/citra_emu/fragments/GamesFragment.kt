// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.fragments

import android.annotation.SuppressLint
import android.net.Uri
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.ViewGroup.MarginLayoutParams
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.edit
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import androidx.preference.PreferenceManager
import androidx.recyclerview.widget.GridLayoutManager
import com.google.android.material.color.MaterialColors
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.transition.MaterialFadeThrough
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import org.citra.citra_emu.BuildConfig
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.adapters.GameAdapter
import org.citra.citra_emu.databinding.FragmentGamesBinding
import org.citra.citra_emu.features.settings.model.BooleanSetting
import org.citra.citra_emu.features.settings.model.CitraSettings
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.utils.AppUpdater
import org.citra.citra_emu.model.Game
import org.citra.citra_emu.utils.BuildUtil
import org.citra.citra_emu.viewmodel.CompressProgressDialogViewModel
import org.citra.citra_emu.viewmodel.GamesViewModel
import org.citra.citra_emu.viewmodel.HomeViewModel

class GamesFragment : Fragment() {
    private var _binding: FragmentGamesBinding? = null
    private val binding get() = _binding!!

    private val gamesViewModel: GamesViewModel by activityViewModels()
    private val homeViewModel: HomeViewModel by activityViewModels()
    private lateinit var gameAdapter: GameAdapter

    private val openImageLauncher = registerForActivityResult(
        ActivityResultContracts.GetContent()
    ) { uri: Uri? ->
        gameAdapter.handleShortcutImageResult(uri)
    }

    private var shouldCompress: Boolean = true
    private var pendingCompressInvocation: String? = null

    /** Platform chip of the library header: 0 = all, 1 = 3DS, 2 = DS/DSi. */
    private var platformFilter = PLATFORM_ALL
    private var shownCount = 0

    companion object {
        private const val PREF_PLATFORM_FILTER = "AzalonHomePlatformFilter"
        private const val PLATFORM_ALL = 0
        private const val PLATFORM_3DS = 1
        private const val PLATFORM_DS = 2

        fun doCompression(
            fragment: Fragment,
            gamesViewModel: GamesViewModel,
            inputPath: String?,
            outputUri: Uri?,
            shouldCompress: Boolean
        ) {
            if (outputUri != null) {
                val outputPath: String =
                    if (!BuildUtil.isGooglePlayBuild) {
                        "!" + NativeLibrary.getNativePath(outputUri)
                    } else {
                        outputUri.toString()
                    }
                CompressProgressDialogViewModel.reset()
                val dialog = CompressProgressDialogFragment.newInstance(shouldCompress, outputPath)
                dialog.showNow(
                    fragment.requireActivity().supportFragmentManager,
                    CompressProgressDialogFragment.TAG
                )

                fragment.lifecycleScope.launch(Dispatchers.IO) {
                    val status = if (shouldCompress) {
                        NativeLibrary.compressFile(inputPath, outputPath)
                    } else {
                        NativeLibrary.decompressFile(inputPath, outputPath)
                    }

                    fragment.requireActivity().runOnUiThread {
                        dialog.dismiss()
                        @Suppress("ktlint:standard:max-line-length")
                        val resId = when (status) {
                            NativeLibrary.CompressStatus.SUCCESS -> if (shouldCompress) R.string.compress_success else R.string.decompress_success
                            NativeLibrary.CompressStatus.COMPRESS_UNSUPPORTED -> R.string.compress_unsupported
                            NativeLibrary.CompressStatus.COMPRESS_ALREADY_COMPRESSED -> R.string.compress_already
                            NativeLibrary.CompressStatus.COMPRESS_FAILED -> R.string.compress_failed
                            NativeLibrary.CompressStatus.DECOMPRESS_UNSUPPORTED -> R.string.decompress_unsupported
                            NativeLibrary.CompressStatus.DECOMPRESS_NOT_COMPRESSED -> R.string.decompress_not_compressed
                            NativeLibrary.CompressStatus.DECOMPRESS_FAILED -> R.string.decompress_failed
                            NativeLibrary.CompressStatus.INSTALLED_APPLICATION -> R.string.compress_decompress_installed_app
                        }

                        MaterialAlertDialogBuilder(fragment.requireContext())
                            .setMessage(fragment.getString(resId))
                            .setPositiveButton(android.R.string.ok, null)
                            .show()

                        gamesViewModel.reloadGames(false)
                    }
                }
            }
        }
    }

    private val onCompressDecompressLauncher = registerForActivityResult(
        ActivityResultContracts.CreateDocument("application/octet-stream")
    ) { uri: Uri? ->
        doCompression(this, gamesViewModel, pendingCompressInvocation, uri, shouldCompress)
        pendingCompressInvocation = null
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enterTransition = MaterialFadeThrough()
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentGamesBinding.inflate(inflater)
        return binding.root
    }

    // This is using the correct scope, lint is just acting up
    @SuppressLint("UnsafeRepeatOnLifecycleDetector")
    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        homeViewModel.setNavigationVisibility(visible = true, animated = true)
        homeViewModel.setStatusBarShadeVisibility(visible = true)

        val inflater = LayoutInflater.from(requireContext())

        shownCount = 0
        gameAdapter = GameAdapter(
            requireActivity() as AppCompatActivity,
            inflater,
            openImageLauncher,
            onRequestCompressOrDecompress = { inputPath, suggestedName, shouldCompress ->
                pendingCompressInvocation = inputPath
                onCompressDecompressLauncher.launch(suggestedName)
                this.shouldCompress = shouldCompress
            }
        )

        binding.gridGames.apply {
            // About one 170dp cover card per column, at least the resource default.
            val columns = maxOf(
                resources.getInteger(R.integer.azalon_game_grid_columns),
                resources.configuration.screenWidthDp / 170
            )
            layoutManager = GridLayoutManager(requireContext(), columns)
            adapter = this@GamesFragment.gameAdapter
        }

        val preferences = PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
        platformFilter = preferences.getInt(PREF_PLATFORM_FILTER, PLATFORM_ALL)
        binding.chipGroupPlatform.check(
            when (platformFilter) {
                PLATFORM_3DS -> R.id.chip_platform_3ds
                PLATFORM_DS -> R.id.chip_platform_ds
                else -> R.id.chip_platform_all
            }
        )
        binding.chipGroupPlatform.setOnCheckedStateChangeListener { _, checkedIds ->
            val selected = when (checkedIds.firstOrNull()) {
                R.id.chip_platform_3ds -> PLATFORM_3DS
                R.id.chip_platform_ds -> PLATFORM_DS
                else -> PLATFORM_ALL
            }
            if (selected == platformFilter) {
                return@setOnCheckedStateChangeListener
            }
            platformFilter = selected
            preferences.edit { putInt(PREF_PLATFORM_FILTER, selected) }
            setAdapter(gamesViewModel.games.value)
            binding.gridGames.scrollToPosition(0)
            binding.gridGames.scheduleLayoutAnimation()
        }

        binding.swipeRefresh.apply {
            // Add swipe down to refresh gesture
            setOnRefreshListener {
                gamesViewModel.reloadGames(false)
            }

            // Set theme color to the refresh animation's background
            setProgressBackgroundColorSchemeColor(
                MaterialColors.getColor(
                    binding.swipeRefresh,
                    com.google.android.material.R.attr.colorPrimary
                )
            )
            setColorSchemeColors(
                MaterialColors.getColor(
                    binding.swipeRefresh,
                    com.google.android.material.R.attr.colorOnPrimary
                )
            )

            // Make sure the loading indicator appears even if the layout is told to refresh before being fully drawn
            post {
                if (_binding == null) {
                    return@post
                }
                binding.swipeRefresh.isRefreshing = gamesViewModel.isReloading.value
            }
        }

        viewLifecycleOwner.lifecycleScope.apply {
            launch {
                repeatOnLifecycle(Lifecycle.State.RESUMED) {
                    gamesViewModel.isReloading.collect { isReloading ->
                        binding.swipeRefresh.isRefreshing = isReloading
                        updateNotice()
                    }
                }
            }
            launch {
                repeatOnLifecycle(Lifecycle.State.RESUMED) {
                    gamesViewModel.games.collectLatest { setAdapter(it) }
                }
            }
            launch {
                repeatOnLifecycle(Lifecycle.State.RESUMED) {
                    gamesViewModel.shouldSwapData.collect {
                        if (it) {
                            setAdapter(gamesViewModel.games.value)
                            gamesViewModel.setShouldSwapData(false)
                        }
                    }
                }
            }
            launch {
                repeatOnLifecycle(Lifecycle.State.RESUMED) {
                    gamesViewModel.shouldScrollToTop.collect {
                        if (it) {
                            scrollToTop()
                            gamesViewModel.setShouldScrollToTop(false)
                        }
                    }
                }
            }
        }

        setInsets()
    }

    private fun isPrereleaseBuild(): Boolean {
        val version = BuildConfig.GIT_VERSION
        return (
            version.contains("alpha") ||
                version.contains("beta") ||
                version.contains("rc") ||
                version.contains("test")
            )
    }

    override fun onResume() {
        super.onResume()

        // Perform update check
        @Suppress("SimplifyBooleanWithConstants", "RedundantSuppression")
        if (!BuildConfig.DEBUG &&
            !BuildUtil.isGooglePlayBuild &&
            CitraSettings.activeSettings.get(BooleanSetting.CHECK_FOR_UPDATES) &&
            !homeViewModel.updatePromptShown
        ) {
            // Check this fork's releases (not upstream Azahar's, whose 2125.x+ tags always
            // looked "newer" than our 1.x versions) and compare against the release tag.
            val checkForPrereleaseUpdates =
                isPrereleaseBuild() ||
                    (CitraSettings.activeSettings.get(IntSetting.UPDATE_CHECK_CHANNEL) == 1)
            viewLifecycleOwner.lifecycleScope.launch {
                val update = AppUpdater.checkForUpdate(
                    includePrereleases = checkForPrereleaseUpdates,
                    manualCheck = false
                ) ?: return@launch
                if (!isAdded) return@launch
                UpdateAvailableNotificationFragment.newInstance(
                    update.tagName,
                    checkForPrereleaseUpdates
                )
                    .show(
                        requireActivity().supportFragmentManager,
                        UpdateAvailableNotificationFragment.TAG
                    )
            }
            homeViewModel.updatePromptShown = true
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }

    private fun setAdapter(games: List<Game>) {
        val preferences = PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
        val library = if (preferences.getBoolean(CitraSettings.PREF_SHOW_HOME_APPS, false)) {
            games
        } else {
            games.filter { !it.isSystemTitle }
        }
        val shown = when (platformFilter) {
            PLATFORM_3DS -> library.filter { !it.isDs }
            PLATFORM_DS -> library.filter { it.isDs }
            else -> library
        }
        val firstLoad = shownCount == 0 && shown.isNotEmpty()
        shownCount = shown.size
        gameAdapter.submitList(shown)
        if (firstLoad) {
            binding.gridGames.scheduleLayoutAnimation()
        }

        binding.textGameCount.text =
            resources.getQuantityString(R.plurals.azalon_game_count, shown.size, shown.size)
        updateNotice()
    }

    private fun updateNotice() {
        if (_binding == null) {
            return
        }
        if (shownCount == 0 && !gamesViewModel.isReloading.value) {
            binding.noticeText.setText(
                if (gamesViewModel.games.value.isNotEmpty()) {
                    R.string.azalon_no_games_for_platform
                } else {
                    R.string.empty_gamelist
                }
            )
            binding.noticeText.visibility = View.VISIBLE
        } else {
            binding.noticeText.visibility = View.INVISIBLE
        }
    }

    private fun scrollToTop() {
        if (_binding != null) {
            binding.appBarGames.setExpanded(true, true)
            binding.gridGames.smoothScrollToPosition(0)
        }
    }

    private fun setInsets() = ViewCompat.setOnApplyWindowInsetsListener(
        binding.root
    ) { view: View, windowInsets: WindowInsetsCompat ->
        val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
        val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())
        val extraListSpacing = resources.getDimensionPixelSize(R.dimen.spacing_large)
        val spacingNavigation = resources.getDimensionPixelSize(R.dimen.spacing_navigation)
        val spacingNavigationRail =
            resources.getDimensionPixelSize(R.dimen.spacing_navigation_rail)

        // The header sits under the status bar; the grid starts right below it.
        val headerPadding = resources.getDimensionPixelSize(R.dimen.spacing_med)
        binding.headerGames.updatePadding(top = barInsets.top + headerPadding)
        binding.gridGames.updatePadding(
            top = headerPadding,
            bottom = barInsets.bottom + spacingNavigation + extraListSpacing
        )

        binding.swipeRefresh.setProgressViewEndTarget(
            false,
            barInsets.top + resources.getDimensionPixelSize(R.dimen.spacing_refresh_end)
        )

        val leftInsets = barInsets.left + cutoutInsets.left
        val rightInsets = barInsets.right + cutoutInsets.right
        val mlpSwipe = binding.swipeRefresh.layoutParams as MarginLayoutParams
        if (view.layoutDirection == View.LAYOUT_DIRECTION_LTR) {
            mlpSwipe.leftMargin = leftInsets + spacingNavigationRail
            mlpSwipe.rightMargin = rightInsets
        } else {
            mlpSwipe.leftMargin = leftInsets
            mlpSwipe.rightMargin = rightInsets + spacingNavigationRail
        }
        binding.swipeRefresh.layoutParams = mlpSwipe

        val mlpHeader = binding.appBarGames.layoutParams as MarginLayoutParams
        mlpHeader.leftMargin = mlpSwipe.leftMargin
        mlpHeader.rightMargin = mlpSwipe.rightMargin
        binding.appBarGames.layoutParams = mlpHeader

        binding.noticeText.updatePadding(bottom = spacingNavigation)

        windowInsets
    }
}
