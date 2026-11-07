// Copyright 2026 Azalon3-DS Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

package org.citra.citra_emu.ds

import android.content.res.ColorStateList
import android.content.res.Configuration
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.TextView
import androidx.annotation.DrawableRes
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialog
import com.google.android.material.color.MaterialColors
import org.citra.citra_emu.R
import org.citra.citra_emu.model.Game
import org.citra.citra_emu.utils.GameIconUtils

/**
 * Pause sheet of the DS frontend: a header with the game, then a grid of action tiles that
 * slide in. Picking a tile dismisses the sheet and runs the action; backing out calls
 * `onCancel`.
 */
class DsPauseMenu(private val activity: AppCompatActivity) {
    class Action(
        @DrawableRes val icon: Int,
        val label: String,
        val destructive: Boolean = false,
        val onClick: () -> Unit
    )

    fun show(
        title: String,
        subtitle: String,
        game: Game?,
        actions: List<Action>,
        onCancel: () -> Unit
    ) {
        val dialog = BottomSheetDialog(activity)
        val view = LayoutInflater.from(activity).inflate(R.layout.ds_pause_menu, null)

        view.findViewById<TextView>(R.id.pause_title).text = title
        view.findViewById<TextView>(R.id.pause_subtitle).text = subtitle
        if (game != null) {
            GameIconUtils.loadGameIcon(activity, game, view.findViewById(R.id.pause_game_icon))
        }

        val landscape =
            activity.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
        view.findViewById<RecyclerView>(R.id.pause_actions).apply {
            layoutManager = GridLayoutManager(activity, if (landscape) 6 else 4)
            adapter = ActionAdapter(actions) { action ->
                dialog.dismiss()
                action.onClick()
            }
        }

        dialog.setContentView(view)
        dialog.setOnCancelListener { onCancel() }
        dialog.behavior.apply {
            skipCollapsed = true
            state = BottomSheetBehavior.STATE_EXPANDED
        }
        dialog.show()
    }

    private class ActionAdapter(
        private val actions: List<Action>,
        private val onPicked: (Action) -> Unit
    ) : RecyclerView.Adapter<ActionAdapter.Holder>() {
        class Holder(view: View) : RecyclerView.ViewHolder(view) {
            val icon: ImageView = view.findViewById(R.id.pause_action_icon)
            val label: TextView = view.findViewById(R.id.pause_action_label)
        }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): Holder = Holder(
            LayoutInflater.from(parent.context).inflate(R.layout.item_pause_action, parent, false)
        )

        override fun getItemCount(): Int = actions.size

        override fun onBindViewHolder(holder: Holder, position: Int) {
            val action = actions[position]
            holder.icon.setImageResource(action.icon)
            holder.label.text = action.label
            val tint = MaterialColors.getColor(
                holder.itemView,
                if (action.destructive) {
                    com.google.android.material.R.attr.colorError
                } else {
                    com.google.android.material.R.attr.colorPrimary
                }
            )
            holder.icon.imageTintList = ColorStateList.valueOf(tint)
            holder.itemView.setOnClickListener { onPicked(action) }
        }
    }
}
