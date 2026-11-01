# SignalGenerator
<br>

### Main branch:
<br>
Frontend for Gra&Afch AD9910 module.<br>
It has touch screen (ILI9488)/Encoder controls.<br>
BCD code output for external filters.<br>
LNA + ATT (Agilent 10-70db ATT) controls.<br><br>

### Screenshots

| Generator | Sweep |
| --- | --- |
| ![Generator screen](scr2.jpg) | ![Sweep screen](scr1.jpg) |

### Frequency editing

- Tap a digit in the Generator frequency or Sweep Start/Stop value to select
  its decimal place. The selected digit is underlined.
- Turn the encoder to change the value by the selected decimal place.
- Press the encoder while a frequency field is selected to cycle through its
  displayed decimal places. The footer arrow buttons still adjust the step
  directly.

### Amplitude calibration

`amplitudeCalibration` in `SignalGenerator.ino` is reserved for measured DDS
gain corrections. It is empty by default, so amplitude commands are unchanged.
Add pairs in the form `{ requested gain, calibrated gain }`, ordered by the
requested value. Exact points are used directly and values between two points
are linearly interpolated. Values outside the measured range remain
uncalibrated.

### AD9851 branch

The `AD9851` branch provides the same interface for a directly connected
AD9851 DDS with a PE4302 or PE4312 digital step attenuator. Current features
were backported from `main` but have not yet been tested on physical hardware.
