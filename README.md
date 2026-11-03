# Modified DroidStar DMR App for iOS & Android

**Version 0.45.05**

A free, open-source digital ham radio app for iPhone and iPad. DMR on BrandMeister, YSF, M17, P25, NXDN, D-STAR, and AllStar, from the phone in your pocket. Built and shared by VU3LVO.

If you want the tour, the Parrot demo, the version history, and the photos, start at **[vu3lvo.com/droidstar](https://vu3lvo.com/droidstar)**. That page is home. This repository is the source.

## Important announcement

**Please note:** I will no longer be maintaining the Android version, at least for the near future. Keeping both iOS and Android moving at the same time turned out to be more than one person can do well, so for now every hour goes into the iPhone and iPad app.

**Special contributor.** A huge thanks and shout-out to VU2TBR (T. Bala). His feedback, and the ideas he kept sending, played a real part in how these features were shaped. A lot of what feels obvious in the app now started as a conversation with him.

---

This project is a modified version of the [original DroidStar app](https://github.com/nostar/DroidStar) by Doug McLain. Doug built the foundation. I have been adding the things I wished a phone could do when I am away from the shack: a cleaner screen, a log that survives a restart, audio that does not embarrass you on a busy Talk Group.

It is licensed under the GPL, it has no ads, and it does not ask you to subscribe. You do need an amateur licence. You do not need a radio or a hotspot. The phone talks to the network the way a hotspot would.

This project uses Qt 6.

## What's new in 0.45.05

One thing, and I wanted it to be the right thing.

Transmit audio had a muffled, close-talk quality to it. Hold the phone near your mouth, key up, and the voice that came out the other end sounded like it was wrapped in a blanket. Reports were kind, then they were specific. So 0.45.05 is an audio pass only. The TX enhancement now lifts that muddiness off the microphone audio before it is encoded, so what leaves the phone is closer to the voice you actually used.

Nothing else moved in this build. The vocoder, receive audio, ODTP, the log, the scanner, all of that is the same app you had in 0.45.04. If you already liked how receive sounded, you should still like it. I would love to know how the new transmit sounds on your radio, your hotspot, and a headset. Those three never quite agree with each other.

The longer story of 0.45.01 through 0.45.04, with the small notes I do not repeat here, is on [vu3lvo.com/droidstar](https://vu3lvo.com/droidstar).

## What you get in the current iOS app

Some of this landed over the last few releases. Some of it has been in the app since the first public builds. I am putting it in one place so you do not have to reconstruct the history from old posts.

- **BrandMeister ODTP.** Open DMR Terminal Protocol, on UDP port 54006, for BrandMeister hosts. Talk Groups change without the old key-up dance. If ODTP is not available, the app falls back to the usual HomeBrew protocol, so you are not stuck staring at a dead link. There is a switch in Settings if you want it off.

- **AllStar, with a password, and IAX WT.** AllStar nodes that expect a password can finally be given one. IAX WT is in there too, along with a round of fixes for connections that used to drop or never quite come up.

- **Live transcription.** You can read a transmission as text while it is still happening. Apple's own speech recognition is one option. The others are models that download once and then run on the phone, which matters when the band is noisy or you are somewhere you cannot have a speaker going. Transcripts can sit with the QSO log. It is still marked beta, and a noisy signal will still fool it now and then. That is the honest version.

- **Talk Group scanner and favourites.** Star the groups you actually use. Scan those, along with what is busy on BrandMeister, and tap once when you hear a conversation you want to join instead of typing a number from memory.

- **Net reminders.** Save the nets you care about. The phone nudges you when it is time to check in, even if you had every intention of remembering and then didn't.

- **Dynamic Island, Notification Center, and the Lock Screen.** This was one of the most requested things, and I get why. TX and RX, the callsign, the Talk Group, visible without opening the app. Useful when the phone is face down on the bench and you only need to know who just came up.

- **HD Voice, on the home page.** Noise reduction on transmit, a clarity pass for your own voice, a mic leveler, and a pre-amp that can add up to 24 dB when the phone mic is just too far away. Each one switches on its own. Headphones are the fair way to judge them.

- **A QSO log that stays put.** Callsign, DMR ID, Talk Group, name, country, and the time in your local zone. It survives a restart. You choose how long it is allowed to grow, and you can turn off logging of your own transmissions if you would rather the log be about everyone else. Export the rows you want as ADIF or CSV, then share the file the way you share anything else on the phone. WhatsApp, mail, AirDrop, whatever you already use.

- **Who is actually calling.** The operator's first name, the handle, shows up under the callsign, looked up from the DMR ID. Country sits beside it. One tap from a log entry opens that station on QRZ, BrandMeister, or APRS.

- **Bluetooth, a wired headset, and background receive.** TX can use the headset mic. RX keeps playing when you leave the app, and it keeps playing when the ringer switch is on silent. I use that constantly, usually while looking up the station I just heard.

- **Recent Talk Groups.** The TG field remembers what you have dialed. Pick from the list. You can stop treating 3100 and 91 and your local group like a phone number you are afraid of forgetting.

- **Last Heard on the home page.** Two rows, so the last couple of stations are visible without opening the log. The AMBE status notice lives in Settings now, where it belongs.

And this is still not the whole list. More is coming. A few of those I would rather show than describe.

## Downloads

The iOS app is on Apple TestFlight. TestFlight only allows 10,000 testers, so I cannot post a public invite link. Invites go out one at a time.

If you want in, request it here: **[vu3lvo.com/droidstar/testflight](https://vu3lvo.com/droidstar/testflight)**

Use your callsign, and the email address on your Apple ID. Then install Apple's free TestFlight app, open the mail when it arrives, and tap Start Testing. That is the whole process. If something about the invite looks wrong, write to me at [hi@vu3lvo.com](mailto:hi@vu3lvo.com).

Android builds from 2024 are no longer something I am shipping or fixing. See the note at the top.

## Feedback and support

Please tell me if 0.45.05 sounds right on your radio. A parrot test on TG 9990 is the quickest way to hear yourself the way everyone else does. If the muffling is gone, I want to know. If it is still there on a particular headset or a particular network, I want to know that more.

Ideas are welcome too. ODTP got built because a tester asked for it and then explained why. That is usually how the good ones start.

## Stay updated

I post progress, new TestFlight builds, and the occasional poll on the WhatsApp channel. [**Join here**](https://inz.to/BVmSa). The same news lands on [vu3lvo.com/droidstar](https://vu3lvo.com/droidstar), if you would rather not be in a chat.

## Acknowledgments

This project is based on the original work by [Doug McLain](https://github.com/nostar/). Many thanks to Doug for the foundation, and for what DroidStar already meant to people before I ever touched it.

Thank you also to BD4VHI, who translated a build into Chinese back in September 2024 and sent it out to operators I could not have reached myself.

## License

This project, including the modifications, is licensed under the GNU General Public License v3.0. See the [LICENSE](LICENSE) file for details.

Your logs, settings, favourite Talk Groups, net reminders, and transcripts stay on the phone. There is no account and no analytics in the app. The networks you connect to still see your callsign and your DMR ID, the same way they would from a radio. The website does not use cookies or analytics either. The longer version is at [vu3lvo.com/droidstar/privacy](https://vu3lvo.com/droidstar/privacy).

## Support

If you would like to support the iOS TestFlight builds, or a public release when that day comes, you can [buy me a coffee](https://buymeacoffee.com/rohithz) or [contribute via PayPal](https://www.paypal.com/ncp/payment/NU89529268M2W). It pays the Apple developer account, a couple of test phones, and the evenings.

---

**73 VU3LVO — Rohith**

---

## Contributors

I am genuinely grateful to the people who have kept this going. Not in the abstract. The Apple bill does not pay itself, and neither do the radios I break while testing. The patrons and the operators who write in with a recording, a bug, or a "this finally sounds like me" are the reason the app is still on the air.

The full list lives on the contributors page, and that page is the one I keep current: **[vu3lvo.com/droidstar/contributors](https://vu3lvo.com/droidstar/contributors)**

As I write this, that wall has 157 contributors, 183 contributions, and callsigns from 18 countries. You can tap a name and hear it in Morse, which is a small thing and also exactly the sort of thing this hobby would want. If your entry needs a change, or you would rather it came down, email [hi@vu3lvo.com](mailto:hi@vu3lvo.com).

A few of the day-one voices, from the first week in September 2024, because I still remember opening those notes:

Ian R., GW4VXE, Joe, Ben, Impossible_Duty_8935, TXDMR, Ray, KC2ZMA, Mark F., Steff, N2HT, ZL2BEZ, K7RAN, WA3ADI, VK4SM, G0BJW, DG9MAQ, VE2KJF, William G., Michael K., W2KDC, VK3TV, AC1SM, HP3ICC.

Everyone else is on the page. Please look. I did not want a README that slowly goes stale while the real list moves.

Want to see your name there? [Buy me a coffee](https://buymeacoffee.com/rohithz), or [contribute via PayPal](https://www.paypal.com/ncp/payment/NU89529268M2W), and help me keep building a digital radio app that a licensed ham can actually use when the expensive radio, the antenna, and the space for either of them are not available.

[vu3lvo.com/droidstar](https://vu3lvo.com/droidstar) · [Request a TestFlight invite](https://vu3lvo.com/droidstar/testflight) · [Contributors](https://vu3lvo.com/droidstar/contributors)
