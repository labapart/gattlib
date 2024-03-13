#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2016-2024, Olivier Martin <olivier@labapart.org>
#

import threading
import time
import traceback

#import dbus.mainloop.glib
from gi.repository import GObject

from . import logger

gobject_mainloop: GObject.MainLoop = None
task_returned_code: int = -1
task_exception: Exception = None

def _user_thread_main(task):
    """Main entry point for the thread that will run user's code."""
    global gobject_mainloop, task_returned_code, task_exception

    try:
        # Wait for GLib main loop to start running before starting user code.
        while True:
            if gobject_mainloop is not None and gobject_mainloop.is_running():
                # Main loop is running, we should be ready to make bluez DBus calls.
                break
            # Main loop isn't running yet, give time back to other threads.
            time.sleep(0)

        # Run user's code.
        task_returned_code = task()
    except Exception as ex:
        logger.error("Exception in %s: %s: %s", task, type(ex), str(ex))
        traceback.print_exception(type(ex), ex, ex.__traceback__)
        task_exception = ex
    finally:
        gobject_mainloop.quit()

def run_mainloop_with(task):
    global gobject_mainloop, task_returned_code, task_exception

    if gobject_mainloop:
        raise RuntimeError("A mainloop is already running")

    # Ensure GLib's threading is initialized to support python threads, and
    # make a default mainloop that all DBus objects will inherit.  These
    # commands MUST execute before any other DBus commands!
    #GObject.threads_init()
    #dbus.mainloop.glib.threads_init()
    # Set the default main loop, this also MUST happen before other DBus calls.
    #mainloop = dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)

    # Create a background thread to run the user task
    user_thread = threading.Thread(target=_user_thread_main, name="gattlib-task", args=(task,))
    user_thread.start()

    # Spin up a GLib main loop in the main thread to process async BLE events.
    gobject_mainloop = GObject.MainLoop()
    try:
        gobject_mainloop.run()  # Doesn't return until the mainloop ends.

        user_thread.join()

        if task_exception:
            raise task_exception

        return task_returned_code
    except KeyboardInterrupt:
        gobject_mainloop.quit()
        # We assume that if we exit with keyboard interrupt than it is not the expected
        # behaviour and we return -1
        return -2
    finally:
        gobject_mainloop = None
