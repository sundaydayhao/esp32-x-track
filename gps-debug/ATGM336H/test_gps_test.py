"""Parser checks only: these synthetic samples do not validate the hardware."""

import unittest

from gps_test import Monitor, coordinate, fields_from_line, parse_sentence


def sentence(body):
    checksum = 0
    for char in body:
        checksum ^= ord(char)
    return f"${body}*{checksum:02X}"


def pair(utc, quality=1, status="A", mode="A", longitude="01131.000"):
    return (
        sentence(f"BDGGA,{utc},4807.038,N,{longitude},E,{quality},08,0.9,545.4,M,46.9,M,,"),
        sentence(f"BDRMC,{utc},{status},4807.038,N,{longitude},E,0.0,0.0,190926,,,{mode}"),
    )


class NmeaTests(unittest.TestCase):
    def test_known_checksum_and_position(self):
        fields = fields_from_line("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47")
        data = parse_sentence(fields)
        self.assertAlmostEqual(data["latitude"], 48.1173)
        self.assertAlmostEqual(data["longitude"], 11.5166666667)

    def test_bad_checksum_rejected(self):
        with self.assertRaises(ValueError):
            fields_from_line("$BDGGA,,,,,,0,00,25.5,,,,,,*00")

    def test_observed_no_fix_is_not_success(self):
        monitor = Monitor()
        for line in ("$BDGGA,,,,,,0,00,25.5,,,,,,*6B", "$BDRMC,,V,,,,,,,,,,N,V*38",
                     "$BDGSA,A,1,,,,,,,,,,,,,25.5,25.5,25.5,4*0B", "$BDGSV,1,1,00,0*74"):
            monitor.feed(line, 0)
        self.assertEqual(monitor.bad_lines + monitor.parse_errors, 0)
        self.assertEqual(monitor.latest["GSA"]["fix_type"], 1)
        self.assertEqual(monitor.max_visible, {"BD": 0})
        self.assertFalse(monitor.summary()["positioning_success"])

    def test_three_distinct_epochs_required(self):
        monitor = Monitor()
        for second in range(3):
            for line in pair(f"1235{19 + second:02d}.00"):
                monitor.feed(line, second)
            self.assertEqual(monitor.summary()["positioning_success"], second == 2)
        self.assertEqual(monitor.paired_fixes, 3)

    def test_repeated_epoch_does_not_count(self):
        monitor = Monitor()
        for second in range(3):
            for line in pair("123519"):
                monitor.feed(line, second)
        self.assertEqual(monitor.paired_fixes, 1)
        self.assertFalse(monitor.summary()["positioning_success"])

    def test_different_epoch_does_not_pair(self):
        monitor = Monitor(1)
        monitor.feed(pair("123519")[0], 0)
        monitor.feed(pair("123520")[1], 0)
        self.assertEqual(monitor.paired_fixes, 0)

    def test_stale_messages_do_not_pair(self):
        monitor = Monitor(1)
        monitor.feed(pair("123519")[0], 0)
        monitor.feed(pair("123519")[1], 4)
        self.assertEqual(monitor.paired_fixes, 0)

    def test_invalid_and_simulated_fixes_rejected(self):
        for kwargs in ({"quality": 0}, {"quality": 6}, {"quality": 8},
                       {"status": "V"}, {"mode": "N"}, {"mode": "S"}, {"mode": "E"}):
            with self.subTest(kwargs=kwargs):
                monitor = Monitor(1)
                for line in pair("123519", **kwargs):
                    monitor.feed(line, 0)
                self.assertFalse(monitor.summary()["positioning_success"])

    def test_disagreeing_coordinates_do_not_pair(self):
        monitor = Monitor(1)
        monitor.feed(pair("123519")[0], 0)
        monitor.feed(pair("123519", longitude="01231.000")[1], 0)
        self.assertEqual(monitor.paired_fixes, 0)

    def test_invalid_message_breaks_streak(self):
        monitor = Monitor()
        for line in pair("123519"):
            monitor.feed(line, 0)
        monitor.feed(pair("123520", status="V")[1], 1)
        for line in pair("123521"):
            monitor.feed(line, 2)
        self.assertEqual(monitor.streak, 1)

    def test_time_gap_breaks_streak(self):
        monitor = Monitor()
        for utc, elapsed in (("123519", 0), ("123523", 4)):
            for line in pair(utc):
                monitor.feed(line, elapsed)
        self.assertEqual(monitor.streak, 1)

    def test_gsv_signal_id_is_not_snr(self):
        data = parse_sentence(fields_from_line(sentence("BDGSV,1,1,01,01,45,100,32,9")))
        self.assertEqual(data["snr"], [32.0])

    def test_coordinate_validation_and_hemispheres(self):
        self.assertAlmostEqual(coordinate("4807.038", "S", True), -48.1173)
        self.assertEqual(coordinate("00000.000", "W", False), 0.0)
        for raw, hemisphere, latitude in (("9060.000", "N", True), ("9100", "N", True),
                                          ("18100.000", "E", False), ("4807.038", "", True),
                                          ("nan", "N", True)):
            with self.assertRaises(ValueError):
                coordinate(raw, hemisphere, latitude)

    def test_malformed_coordinate_never_proves_fix(self):
        monitor = Monitor(1)
        for line in pair("123519", longitude="18131.000"):
            monitor.feed(line, 0)
        self.assertEqual(monitor.parse_errors, 2)
        self.assertFalse(monitor.summary()["positioning_success"])


if __name__ == "__main__":
    unittest.main()
