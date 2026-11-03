# DroidStar DMR for iOS

**Version 0.45.05** · by VU3LVO (Rohith Namboothiri)

DroidStar DMR puts digital amateur radio on your iPhone and iPad. Talk worldwide over DMR, read incoming audio as live text, scan Talk Groups, and follow every QSO from the Dynamic Island. It is free, open source (GPL-3.0), and used by 10,000+ licensed hams. You need a licence. You do not need a radio or a hotspot.

**The home for this project is [vu3lvo.com/droidstar](https://vu3lvo.com/droidstar).** Feature tour, version history, Parrot demo, privacy policy, and how to get on the air all live there. This repository is the source.

This is a fork of [Doug McLain’s DroidStar](https://github.com/nostar/DroidStar). It is built with Qt 6 for iOS 14 and later.

---

## Get the iOS app

DroidStar for iOS is distributed through Apple TestFlight. Apple allows only 10,000 testers, so invites are sent one by one. There is no public TestFlight link.

Request an invite at **[vu3lvo.com/droidstar/testflight](https://vu3lvo.com/droidstar/testflight)**.

1. Fill in the form with your callsign and the email address of your Apple ID.
2. Install Apple’s free TestFlight app from the App Store.
3. Open the invite email, tap **Start Testing**, and install DroidStar.

Questions: [hi@vu3lvo.com](mailto:hi@vu3lvo.com)

Android development is paused while the iPhone and iPad app is the focus. A new Android build, and a web version, are on the roadmap at [vu3lvo.com/droidstar](https://vu3lvo.com/droidstar).

---

## What’s new in 0.45.05

Audio only. Transmit voice was coming out muffled, especially on close talk into the mic. 0.45.05 improves that TX enhancement so the muddiness is taken off the microphone audio before it is encoded. The vocoder, receive audio, and every other feature are the same as 0.45.04.

The full notes for 0.45.01 through 0.45.04 are on [vu3lvo.com/droidstar](https://vu3lvo.com/droidstar).

---

## Also in the 0.45 line

- **BrandMeister ODTP.** Open DMR Terminal Protocol on UDP 54006, with automatic fallback to the standard HomeBrew protocol, and a Settings switch to turn ODTP on or off.
- **AllStar.** An AllStar password field, IAX WT support, and fixes for IAX connections.
- **Multilingual live transcription.** Apple’s on-device speech recognition, or a downloaded model that runs entirely on the phone. Transcripts can be kept with the QSO log. The feature is still marked beta.
- **Talk Group scanner and favourites.** Star the groups you care about, scan them with global BrandMeister activity, and tap once to join a conversation.
- **Net reminders.** Save net schedules and get a notification when it is time to check in.
- **Dynamic Island, Notification Center, and Lock Screen.** TX/RX state, callsign, and Talk Group without opening the app.
- **QSO log.** Callsign, DMR ID, Talk Group, name, country, and local time, kept across restarts. Set the log length, turn self-TX logging on or off, export selected rows as ADIF or CSV, and share them. One tap opens QRZ, BrandMeister, or APRS.
- **Who’s calling.** The operator’s name and country are looked up from the DMR ID and shown under the callsign.
- **Bluetooth, wired headsets, and background RX.** Headset microphones are used for TX. Receive keeps playing in the background and in silent mode.
- **Recent Talk Groups.** The TG field remembers what you have dialed, so you do not have to keep the numbers in your head.

### Modes

DMR on BrandMeister (ODTP and HomeBrew), YSF, M17, P25, NXDN, D-STAR reflectors (REF, XRF, DCS), and AllStar over IAX.

DMR text messaging is on the roadmap. It is not in 0.45.05.

---

## Feedback

If the new TX audio still sounds muffled on your radio or hotspot, say so. Send notes through TestFlight, open an issue on this repository, or email [hi@vu3lvo.com](mailto:hi@vu3lvo.com). A lot of what is already in the app, including ODTP, started as a tester request.

Join the WhatsApp channel for release news and polls: [inz.to/BVmSa](https://inz.to/BVmSa). The same news is posted at [vu3lvo.com/droidstar](https://vu3lvo.com/droidstar).

---

## Acknowledgments

This project stands on [Doug McLain’s DroidStar](https://github.com/nostar/). Thank you, Doug, for the foundation and for what it has meant to digital voice.

Thank you to **VU2TBR (T. Bala)** for the feedback and ideas that shaped a lot of the early design, and to **BD4VHI** for the Chinese translation of the September 2024 build.

---

## Contributors

DroidStar stays free, with no ads and no subscription, because hams pay for the Apple developer account, test radios, and the time to keep shipping.

The full wall is at **[vu3lvo.com/droidstar/contributors](https://vu3lvo.com/droidstar/contributors)**: 157 contributors, 183 contributions, and callsigns from 18 countries. That page is the list of record. Names there can be heard in Morse, and a note to [hi@vu3lvo.com](mailto:hi@vu3lvo.com) changes or removes an entry.

If DroidStar has a place on your home screen, a coffee keeps it on the air:

- [Buy me a coffee](https://buymeacoffee.com/rohithz)
- [PayPal](https://www.paypal.com/ncp/payment/NU89529268M2W)

---

## Privacy

DroidStar has no accounts, analytics, or tracking. QSO logs, settings, favourite Talk Groups, net reminders, and transcripts stay on the device. The app talks directly to the networks you choose, the same way a radio would. The website uses no cookies or analytics.

Details: [vu3lvo.com/droidstar/privacy](https://vu3lvo.com/droidstar/privacy)

---

## License

This project, including these modifications, is licensed under the GNU General Public License v3.0. See the [LICENSE](LICENSE) file for details.

---

**73 de VU3LVO — Rohith**

[vu3lvo.com/droidstar](https://vu3lvo.com/droidstar) · [TestFlight invite](https://vu3lvo.com/droidstar/testflight) · [Contributors](https://vu3lvo.com/droidstar/contributors)
