# SPDX-License-Identifier: MIT
#
# Driving the game from a script, for benchmarks that need it in a known place.
#
# Every rule below was learned by getting it wrong, and most of them fail
# silently rather than loudly, so this exists once rather than once per script:
#
#   * `xdotool key --window` sends XSendEvent, which SDL ignores. Without
#     --window it goes through XTEST, which SDL honours.
#   * xdotool cannot reach native Wayland windows at all, so SDL has to be
#     forced onto X11/XWayland.
#   * `windowactivate` needs a window manager advertising _NET_ACTIVE_WINDOW.
#     A bare X server has none, so use windowfocus.
#   * `xdotool click` is shorter than the game's input polling interval, so the
#     press is never observed. Press and release have to be separate events
#     with a hold in between.
#   * `( cmd ) &` gives the subshell's PID, not the game's. Killing it leaves
#     the game running, and the next measurement lands on top of the last one.
#     `( exec cmd ) &` is the fix.
#   * The attract demo alternates with the menu on its own schedule, so timing a
#     fixed window there lands on 2D menu frames as often as on the 3D scene.
#     Start a real race instead.

# Where "RACE" sits in the game's 640x480 logical frame.
GAME_RACE_X_FRACTION=0.208
GAME_RACE_Y_FRACTION=0.694

# SDL must be on X11 for xdotool to reach the window at all.
export SDL_VIDEODRIVER=x11

# Benchmarks are driven headlessly, often over SSH, and often on a machine
# somebody is sitting at. Rendering to an offscreen X server hides the picture
# but not the sound: without this, every run blares out of the real speakers.
# The dummy driver still runs the audio callback, so the game's audio path --
# which calls back into the translated code -- is still exercised.
export SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-dummy}

game_require_tools() {
	if ! command -v xdotool > /dev/null 2>&1; then
		echo "xdotool not found -- needed to drive the game. sudo apt install xdotool" >&2
		return 1
	fi
	if [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ]; then
		echo "No DISPLAY or WAYLAND_DISPLAY -- nothing to drive." >&2
		return 1
	fi
}

# Any stray instance would fight this one for the GPU and wreck the numbers.
game_kill() {
	pkill -x nfs2se 2>/dev/null || true
	for _ in $(seq 20); do
		pgrep -x nfs2se > /dev/null 2>&1 || return 0
		sleep 0.25
	done
	pkill -9 -x nfs2se 2>/dev/null || true
	sleep 0.5
}

# Echoes the game's window id, or nothing if it never appeared.
game_wait_window() {
	local win=""
	for _ in $(seq 20); do
		win=$(xdotool search --name "Need For Speed" 2>/dev/null | head -1 || true)
		[ -n "$win" ] && break
		sleep 1
	done
	printf '%s' "$win"
}

# Clicks a point given in the game's 640x480 logical frame, mapping it through
# the window's real geometry and the 4:3 letterbox the game keeps inside it.
game_click_logical() {
	local win=$1 fx=$2 fy=$3
	local X Y WIDTH HEIGHT

	eval "$(xdotool getwindowgeometry --shell "$win")"

	# Largest 4:3 box centred in the window, matching KeepAspectRatio=1.
	local content_w content_h off_x off_y px py
	content_w=$(awk -v w="$WIDTH" -v h="$HEIGHT" 'BEGIN{ c=h*4/3; print (c<w)?c:w }')
	content_h=$(awk -v cw="$content_w" 'BEGIN{ print cw*3/4 }')
	off_x=$(awk -v w="$WIDTH"  -v c="$content_w" 'BEGIN{ print (w-c)/2 }')
	off_y=$(awk -v h="$HEIGHT" -v c="$content_h" 'BEGIN{ print (h-c)/2 }')
	px=$(awk -v x="$X" -v o="$off_x" -v c="$content_w" -v f="$fx" 'BEGIN{ printf "%d", x+o+c*f }')
	py=$(awk -v y="$Y" -v o="$off_y" -v c="$content_h" -v f="$fy" 'BEGIN{ printf "%d", y+o+c*f }')

	xdotool mousemove --sync "$px" "$py" 2>/dev/null || true
	sleep 0.5
	xdotool mousedown 1 2>/dev/null || true
	sleep 0.25
	xdotool mouseup 1 2>/dev/null || true
}

# Gets past the intro movie and into a race. Exactly ONE Escape: at the menu a
# second one opens "exit to system?".
game_start_race() {
	local win=$1
	xdotool windowfocus --sync "$win" 2>/dev/null || true
	xdotool key Escape 2>/dev/null || true
	sleep 8
	xdotool windowfocus --sync "$win" 2>/dev/null || true
	game_click_logical "$win" "$GAME_RACE_X_FRACTION" "$GAME_RACE_Y_FRACTION"
}
