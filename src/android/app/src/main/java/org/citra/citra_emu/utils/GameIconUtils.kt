// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

package org.citra.citra_emu.utils

import android.graphics.Bitmap
import android.widget.ImageView
import androidx.core.graphics.drawable.toDrawable
import androidx.fragment.app.FragmentActivity
import coil.ImageLoader
import coil.decode.DataSource
import coil.fetch.DrawableResult
import coil.fetch.FetchResult
import coil.fetch.Fetcher
import coil.key.Keyer
import coil.memory.MemoryCache
import coil.request.ImageRequest
import coil.request.Options
import coil.transform.RoundedCornersTransformation
import java.nio.IntBuffer
import org.citra.citra_emu.R
import org.citra.citra_emu.model.Game

class GameIconFetcher(private val game: Game, private val options: Options) : Fetcher {
    override suspend fun fetch(): FetchResult = DrawableResult(
        drawable = getGameIcon(game.icon)!!.toDrawable(options.context.resources),
        isSampled = false,
        dataSource = DataSource.DISK
    )

    private fun getGameIcon(vector: IntArray?): Bitmap? {
        if (vector == null) {
            return null
        }
        if (game.isDs) {
            return getDsIcon(vector)
        }
        val bitmap = Bitmap.createBitmap(48, 48, Bitmap.Config.RGB_565)
        bitmap.copyPixelsFromBuffer(IntBuffer.wrap(vector))
        return bitmap
    }

    /**
     * DS banner icons are 32x32 ARGB8888 (0xAARRGGBB, as Bitmap expects), not the
     * 48x48 RGB565 of 3DS SMDH icons. Scaled up without filtering so the pixel art
     * stays sharp when the list stretches it.
     */
    private fun getDsIcon(argb: IntArray): Bitmap? {
        if (argb.size < DS_ICON_SIZE * DS_ICON_SIZE) {
            return null
        }
        val icon = Bitmap.createBitmap(argb, DS_ICON_SIZE, DS_ICON_SIZE, Bitmap.Config.ARGB_8888)
        val scaled = DS_ICON_SIZE * DS_ICON_SCALE
        return Bitmap.createScaledBitmap(icon, scaled, scaled, false)
    }

    private companion object {
        const val DS_ICON_SIZE = 32
        const val DS_ICON_SCALE = 4
    }

    class Factory : Fetcher.Factory<Game> {
        override fun create(data: Game, options: Options, imageLoader: ImageLoader): Fetcher =
            GameIconFetcher(data, options)
    }
}

class GameIconKeyer : Keyer<Game> {
    override fun key(data: Game, options: Options): String = data.path
}

object GameIconUtils {
    fun loadGameIcon(
        activity: FragmentActivity,
        game: Game,
        imageView: ImageView,
        rounded: Boolean = true
    ) {
        val imageLoader = ImageLoader.Builder(activity)
            .components {
                add(GameIconKeyer())
                add(GameIconFetcher.Factory())
            }
            .memoryCache {
                MemoryCache.Builder(activity)
                    .maxSizePercent(0.25)
                    .build()
            }
            .build()

        val request = ImageRequest.Builder(activity)
            .data(game)
            .target(imageView)
            .error(R.drawable.no_icon)
            .apply {
                if (rounded) {
                    transformations(
                        RoundedCornersTransformation(
                            activity.resources.getDimensionPixelSize(R.dimen.spacing_med)
                                .toFloat()
                        )
                    )
                }
            }
            .build()
        imageLoader.enqueue(request)
    }
}
