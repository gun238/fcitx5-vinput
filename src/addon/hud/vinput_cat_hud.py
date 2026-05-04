#!/usr/bin/env python3

import os
import sys

import gi

gi.require_version("Gdk", "3.0")
gi.require_version("Gtk", "3.0")
gi.require_version("PangoCairo", "1.0")
from gi.repository import Gdk, GLib, Gtk, Pango, PangoCairo


BASE_DIR = os.path.dirname(os.path.abspath(__file__))
GIF_PATH = os.path.join(BASE_DIR, "cat-typing.gif")
BUBBLE_WIDTH = 72
BUBBLE_HEIGHT = 36
BUBBLE_SCALE = BUBBLE_HEIGHT / 36
BUBBLE_FONT_SIZE = max(8, int(11 * BUBBLE_SCALE))


class Bubble(Gtk.DrawingArea):
    def __init__(self, text: str) -> None:
        super().__init__()
        self.text = text
        self.set_halign(Gtk.Align.START)
        self.set_valign(Gtk.Align.CENTER)
        self.set_hexpand(False)
        self.set_vexpand(False)
        self.set_size_request(BUBBLE_WIDTH, BUBBLE_HEIGHT)

    def set_text(self, text: str) -> None:
        self.text = text
        self.set_size_request(BUBBLE_WIDTH, BUBBLE_HEIGHT)
        self.queue_resize()
        self.queue_draw()

    def do_draw(self, cr) -> bool:
        allocation = self.get_allocation()
        width = min(allocation.width, BUBBLE_WIDTH)
        height = min(allocation.height, BUBBLE_HEIGHT)
        origin_x = max(0.0, (allocation.width - width) / 2.0)
        origin_y = max(0.0, (allocation.height - height) / 2.0)
        scale = BUBBLE_SCALE
        stroke = max(1.0, min(4.0 * scale, height * 0.22))
        margin = stroke / 2.0
        tail_len = max(5.0, min(12.0 * scale, width * 0.22))
        tail_h = max(5.0, min(10.0 * scale, height * 0.45))
        x = origin_x + margin + tail_len
        y = origin_y + margin
        w = max(1.0, width - tail_len - stroke)
        body_h = max(1.0, height - stroke)
        corner = max(1.0, min(5.0 * scale, body_h / 2.0, w / 2.0))
        step = max(1.0, min(2.0 * scale, body_h / 3.0, w / 3.0))

        cr.set_antialias(0)

        def bubble_path(offset_x: float = 0.0, offset_y: float = 0.0) -> None:
            bx = x + offset_x
            by = y + offset_y
            tail_tip_x = origin_x + margin + offset_x
            tail_mid_y = by + body_h * 0.58
            tail_top_y = tail_mid_y - tail_h / 2.0
            tail_bottom_y = tail_mid_y + tail_h / 2.0
            bottom = by + body_h
            cr.new_path()
            cr.move_to(bx + corner, by)
            cr.line_to(bx + w - corner, by)
            cr.line_to(bx + w - corner, by + step)
            cr.line_to(bx + w, by + step)
            cr.line_to(bx + w, bottom - step)
            cr.line_to(bx + w - corner, bottom - step)
            cr.line_to(bx + w - corner, bottom)
            cr.line_to(bx + corner, bottom)
            cr.line_to(bx + corner, bottom - step)
            cr.line_to(bx, bottom - step)
            cr.line_to(bx, tail_bottom_y)
            cr.line_to(tail_tip_x, tail_mid_y)
            cr.line_to(bx, tail_top_y)
            cr.line_to(bx, by + step)
            cr.line_to(bx + corner, by + step)
            cr.close_path()

        bubble_path(1.5 * scale, 1.5 * scale)
        cr.set_source_rgba(0.0, 0.0, 0.0, 0.10)
        cr.fill()

        bubble_path()
        cr.set_source_rgba(1.0, 1.0, 1.0, 0.98)
        cr.fill_preserve()
        cr.set_source_rgba(0.02, 0.02, 0.02, 0.96)
        cr.set_line_width(4.0 * scale)
        cr.stroke()

        layout = self.create_pango_layout(self.text)
        layout.set_font_description(Pango.FontDescription(f"Sans Bold {BUBBLE_FONT_SIZE}"))
        text_w, text_h = layout.get_pixel_size()
        cr.set_source_rgba(0.02, 0.02, 0.02, 1.0)
        cr.move_to(x + (w - text_w) / 2, y + (body_h - text_h) / 2 - 0.5 * scale)
        PangoCairo.show_layout(cr, layout)
        return False


class CatHud:
    def __init__(self) -> None:
        self.window = Gtk.Window(type=Gtk.WindowType.POPUP)
        self.window.set_decorated(False)
        self.window.set_resizable(False)
        self.window.set_keep_above(True)
        self.window.set_skip_taskbar_hint(True)
        self.window.set_skip_pager_hint(True)
        self.window.set_app_paintable(True)
        self.window.set_accept_focus(False)
        self.window.set_focus_on_map(False)

        screen = self.window.get_screen()
        visual = screen.get_rgba_visual()
        if visual is not None:
            self.window.set_visual(visual)

        css = b"""
        window {
          background: transparent;
        }
        #root {
          background: transparent;
        }
        """
        provider = Gtk.CssProvider()
        provider.load_from_data(css)
        Gtk.StyleContext.add_provider_for_screen(
            screen, provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )

        root = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        root.set_name("root")
        root.set_margin_start(0)
        root.set_margin_end(0)
        root.set_margin_top(0)
        root.set_margin_bottom(0)

        self.image = Gtk.Image.new_from_file(GIF_PATH)
        self.image.set_halign(Gtk.Align.START)
        self.image.set_valign(Gtk.Align.CENTER)
        self.image.set_hexpand(False)
        self.image.set_vexpand(False)
        root.pack_start(self.image, False, False, 0)

        self.bubble = Bubble("listening")
        self.bubble.set_size_request(BUBBLE_WIDTH, BUBBLE_HEIGHT)
        root.pack_start(self.bubble, False, False, 0)

        self.window.add(root)
        self.window.connect("draw", self._draw)
        self.window.show_all()
        self.window.hide()

    def _draw(self, _widget, cr) -> bool:
        cr.set_source_rgba(0, 0, 0, 0)
        cr.set_operator(0)
        cr.paint()
        cr.set_operator(2)
        return False

    def _fallback_position(self) -> tuple[int, int]:
        display = Gdk.Display.get_default()
        if display is None:
            return 120, 120
        seat = display.get_default_seat()
        pointer = seat.get_pointer() if seat else None
        if pointer is None:
            return 120, 120
        _screen, x, y = pointer.get_position()
        return int(x), int(y)

    def show(self, state: str, x: int, y: int) -> None:
        if state not in {"listening", "typing"}:
            state = "listening"
        self.bubble.set_text(state)
        self.window.show_all()

        while Gtk.events_pending():
            Gtk.main_iteration_do(False)

        if x <= 0 and y <= 0:
            x, y = self._fallback_position()

        width, height = self.window.get_size()
        screen = self.window.get_screen()
        monitor = screen.get_monitor_at_point(x, y)
        area = screen.get_monitor_workarea(monitor)

        target_x = x + 14
        target_y = y + 18
        target_x = min(max(area.x, target_x), area.x + area.width - width)
        target_y = min(max(area.y, target_y), area.y + area.height - height)
        self.window.move(target_x, target_y)
        self.window.present()

    def hide(self) -> None:
        self.window.hide()


hud = CatHud()


def handle_stdin(_source, _condition):
    line = sys.stdin.readline()
    if line == "":
        Gtk.main_quit()
        return False

    parts = line.strip().split()
    if not parts:
        return True
    if parts[0] == "hide":
        hud.hide()
        return True
    if parts[0] == "show" and len(parts) >= 4:
        try:
            x = int(parts[2])
            y = int(parts[3])
        except ValueError:
            x, y = 0, 0
        hud.show(parts[1], x, y)
    return True


GLib.io_add_watch(sys.stdin, GLib.IO_IN | GLib.IO_HUP, handle_stdin)
Gtk.main()
