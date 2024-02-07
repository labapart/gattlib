#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2016-2024, Olivier Martin <olivier@labapart.org>
#

import struct
import sys
import threading

from dbus.mainloop.glib import DBusGMainLoop
try:
  from gi.repository import GLib, GObject
except ImportError:
  import gobject as GObject
import sys

import numpy
from matplotlib.pylab import *
from mpl_toolkits.axes_grid1 import host_subplot
import matplotlib.animation as animation

from gattlib import uuid

last_measures = {
    'temperature': { 'value': None, 'min': None, 'max': None },
    'pressure': { 'value': None, 'min': None, 'max': None },
    'humidity': { 'value': None, 'min': None, 'max': None },
}


def temperature_notification(value, user_data):
    last_measures['temperature']['value'] = float("%d.%d" % (value[0], value[1]))
    print("Temperature: %f" % last_measures['temperature']['value'])


def pressure_notification(value, user_data):
    (pressure_integer, pressure_decimal) = struct.unpack("<IB", value)
    last_measures['pressure']['value'] = float("%d.%d" % (pressure_integer, pressure_decimal))
    print("Pressure: %f" % last_measures['pressure']['value'])


def humidity_notification(value, user_data):
    last_measures['humidity']['value'] = value[0]
    print("Humidity: %d%%" % last_measures['humidity']['value'])


# Data Placeholders
temperature = zeros(0)
humidity = zeros(0)
t = zeros(0)
x = 0.0
xmax = 1000.0
temp_line = None
hum_line = None
ax_temp = None
ax_hum = None
simulation = None


def graph_init():
    global x
    global temperature, humidity, t
    global temp_line, hum_line
    global ax_temp, ax_hum
    global simulation

    font = {'size': 9}
    matplotlib.rc('font', **font)

    # Setup figure and subplots
    f0 = figure(num=0, figsize=(12, 8))  # , dpi = 100)
    f0.suptitle("Nordic Thingy", fontsize=12)
    ax_temp = host_subplot(111)
    ax_hum = ax_temp.twinx()

    # Set titles of subplots
    ax_temp.set_title('Temperature/Humidity vs Time')

    # set y-limits
    ax_temp.set_ylim(0, 45)
    ax_hum.set_ylim(0, 100)

    # sex x-limits
    ax_temp.set_xlim(0, xmax)
    ax_hum.set_xlim(0, xmax)

    # Turn on grids
    ax_temp.grid(True)

    # set label names
    ax_temp.set_xlabel("t")
    ax_temp.set_ylabel("temperature")
    ax_hum.set_ylabel("humidity")

    temp_line, = ax_temp.plot(t, temperature, 'b-', label="temperature")
    hum_line, = ax_hum.plot(t, humidity, 'g-', label="humidity")

    # set lagends
    ax_temp.legend([temp_line, hum_line], [temp_line.get_label(), hum_line.get_label()])

    # interval: draw new frame every 'interval' ms
    # Note: We expose simulation to prevent Python garbage collector to rmeove it!
    simulation = animation.FuncAnimation(f0, graph_update, blit=False, interval=20)

    plt.show()


def graph_update(self):
    global x, xmax
    global temperature, humidity, t
    global temp_line, hum_line

    if last_measures['temperature']['value']:
        temperature = append(temperature, last_measures['temperature']['value'])
        if last_measures['temperature']['min']:
            last_measures['temperature']['min'] = min(last_measures['temperature']['min'], last_measures['temperature']['value'] - 5)
        else:
            last_measures['temperature']['min'] = last_measures['temperature']['value'] - 5
        if last_measures['temperature']['max']:
            last_measures['temperature']['max'] = max(last_measures['temperature']['max'], last_measures['temperature']['value'] + 5)
        else:
            last_measures['temperature']['max'] = last_measures['temperature']['value'] + 5

        ax_temp.set_ylim(last_measures['temperature']['min'], last_measures['temperature']['max'])

    if last_measures['humidity']['value']:
        humidity = append(humidity, last_measures['humidity']['value'])
        if last_measures['humidity']['min']:
            last_measures['humidity']['min'] = min(last_measures['humidity']['min'], last_measures['humidity']['value'] - 20)
        else:
            last_measures['humidity']['min'] = last_measures['humidity']['value'] - 20
        if last_measures['humidity']['max']:
            last_measures['humidity']['max'] = max(last_measures['humidity']['max'], last_measures['humidity']['value'] + 20)
        else:
            last_measures['humidity']['max'] = last_measures['humidity']['value'] + 20

        ax_hum.set_ylim(last_measures['humidity']['min'], last_measures['humidity']['max'])

    t = append(t, x)

    x += 0.05

    temp_line.set_data(t, temperature)
    hum_line.set_data(t, humidity)

    if x >= xmax - 1.00:
        temp_line.axes.set_xlim(x - xmax + 1.0, x + 1.0)
        hum_line.axes.set_xlim(x - xmax + 1.0, x + 1.0)

    return temp_line, hum_line


def environment_service(args, gatt_device):
    NORDIC_THINGY_WEATHER_STATION_SERVICE = uuid.gattlib_uuid_str_to_int("EF680200-9B35-4933-9B10-52FFA9740042")
    NORDIC_THINGY_TEMPERATURE_CHAR = uuid.gattlib_uuid_str_to_int("EF680201-9B35-4933-9B10-52FFA9740042")
    NORDIC_THINGY_PRESSURE_CHAR = uuid.gattlib_uuid_str_to_int("EF680202-9B35-4933-9B10-52FFA9740042")
    NORDIC_THINGY_HUMIDITY_CHAR = uuid.gattlib_uuid_str_to_int("EF680203-9B35-4933-9B10-52FFA9740042")
    NORDIC_THINGY_AIR_QUALITY_CHAR = uuid.gattlib_uuid_str_to_int("EF680204-9B35-4933-9B10-52FFA9740042")

    temperature_characteristic = gatt_device.characteristics[NORDIC_THINGY_TEMPERATURE_CHAR]
    pressure_characteristic = gatt_device.characteristics[NORDIC_THINGY_PRESSURE_CHAR]
    humidity_characteristic = gatt_device.characteristics[NORDIC_THINGY_HUMIDITY_CHAR]
    air_quality_characteristic = gatt_device.characteristics[NORDIC_THINGY_AIR_QUALITY_CHAR]

    # Initialize graph
    threading.Thread(target=graph_init).start()

    try:
        DBusGMainLoop(set_as_default=True)
        mainloop = GLib.MainLoop()

        temperature_characteristic.register_notification(temperature_notification)
        temperature_characteristic.notification_start()

        pressure_characteristic.register_notification(pressure_notification)
        pressure_characteristic.notification_start()

        humidity_characteristic.register_notification(humidity_notification)
        humidity_characteristic.notification_start()

        mainloop.run()
    except KeyboardInterrupt:
        mainloop.quit()
    finally:
        humidity_characteristic.notification_stop()
        pressure_characteristic.notification_stop()
        temperature_characteristic.notification_stop()
        gatt_device.disconnect()
