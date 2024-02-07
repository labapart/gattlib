#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2016-2024, Olivier Martin <olivier@labapart.org>
#

import time
import threading
import wave

from gattlib import uuid

from dbus.mainloop.glib import DBusGMainLoop
try:
  from gi.repository import GLib, GObject
except ImportError:
  import gobject as GObject

m_thingy_buffer_free = threading.Event()
m_mainloop = None


def speaker_status_notification(value, user_data):
    global m_thingy_buffer_free

    if value == b'\x01':
        print("Thingy's Buffer warning")
        m_thingy_buffer_free.clear()
    elif value == b'\x02':
        print("Thingy's Buffer ready")
        m_thingy_buffer_free.set()
    elif value == b'\x10':
        print("Thingy's Packet disregarded")
    elif value == b'\x11':
        print("Thingy's Invalid command")
    elif value == b'\x00':
        print("Thingy's Finished")
    else:
        raise RuntimeError("Invalid Speaker notification value: %s" % value)


def play_sample(config_characteristic, speaker_characteristic):
    # Read the current configuration and only change the speaker configuration (not the microphone configuration)
    sound_config = config_characteristic.read()
    sound_config[0] = 0x03
    config_characteristic.write(sound_config)
    # Test speaker
    speaker_characteristic.write(b'\x03')

    # Wait a bit before finishing
    time.sleep(1)

    m_mainloop.quit()


def play_wav_file(config_characteristic, speaker_characteristic, wav_filepath):
    global m_thingy_buffer_free

    wav_file = wave.open(wav_filepath)

    # Python library only support non-compressed WAV file
    if wav_file.getcomptype() != 'NONE':
        raise RuntimeError("Please give a non-compressed WAV file")
    if wav_file.getsampwidth() != 1:
        raise RuntimeError("Nordic Thingy52 only supports 8-bit WAV file")
    if wav_file.getframerate() != 8000:
        raise RuntimeError("Nordic Thingy52 only supports 8kHz WAV file")
    if wav_file.getnchannels() == 2:
        print("Warning: Your WAV file is a stereo file")

    frames = wav_file.readframes(wav_file.getnframes())

    # Read the current configuration and only change the speaker configuration (not the microphone configuration)
    sound_config = config_characteristic.read()
    sound_config[0] = 0x02
    config_characteristic.write(sound_config)

    stream = speaker_characteristic.stream_open()

    # We assume the buffer is free when we start
    m_thingy_buffer_free.set()

    # We send one frame at a time
    max_frame_size = stream.mtu * 1

    while len(frames) > 0:
        if not m_thingy_buffer_free.is_set():
            m_thingy_buffer_free.wait()

        stream.write(frames[0:max_frame_size])
        frames = frames[max_frame_size:]

        # Arbitraty value
        time.sleep(0.03)

    stream.close()
    print("All WAV file has been sent")

    # Wait a bit before finishing
    time.sleep(1)

    m_mainloop.quit()


def sound_service(args, gatt_device):
    global m_mainloop

    NORDIC_THINGY_SOUND_SERVICE = uuid.gattlib_uuid_str_to_int("EF680500-9B35-4933-9B10-52FFA9740042")
    NORDIC_THINGY_CONFIG_CHAR = uuid.gattlib_uuid_str_to_int("EF680501-9B35-4933-9B10-52FFA9740042")
    NORDIC_THINGY_SPEAKER_CHAR = uuid.gattlib_uuid_str_to_int("EF680502-9B35-4933-9B10-52FFA9740042")
    NORDIC_THINGY_SPEAKER_STATUS_CHAR = uuid.gattlib_uuid_str_to_int("EF680503-9B35-4933-9B10-52FFA9740042")
    NORDIC_THINGY_MICROPHONE_CHAR = uuid.gattlib_uuid_str_to_int("EF680504-9B35-4933-9B10-52FFA9740042")

    config_characteristic = gatt_device.characteristics[NORDIC_THINGY_CONFIG_CHAR]
    speaker_characteristic = gatt_device.characteristics[NORDIC_THINGY_SPEAKER_CHAR]
    speaker_status_characteristic = gatt_device.characteristics[NORDIC_THINGY_SPEAKER_STATUS_CHAR]
    microphone_characteristic = gatt_device.characteristics[NORDIC_THINGY_MICROPHONE_CHAR]

    try:
        DBusGMainLoop(set_as_default=True)
        m_mainloop = GLib.MainLoop()

        speaker_status_characteristic.register_notification(speaker_status_notification)
        speaker_status_characteristic.notification_start()

        if args.wav:
            threading.Thread(target=play_wav_file, args=(config_characteristic, speaker_characteristic, args.wav)).start()
        else:
            threading.Thread(target=play_sample, args=(config_characteristic, speaker_characteristic)).start()

        m_mainloop.run()
    except KeyboardInterrupt:
        m_mainloop.quit()
    finally:
        speaker_status_characteristic.notification_stop()
        gatt_device.disconnect()
