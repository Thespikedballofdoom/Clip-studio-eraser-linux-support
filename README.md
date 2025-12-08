Horrific but functional vibecoded program to send CTRL and sometimes ALT events to clip studio paint. It writes to the keyboard event stream and also xdotool to do it because clip studio throws away input from just one device for some reason.

There are more elegant ways to do this probably, but this was the only way I could make it work with my stupidity. There's no 'eraser' function like the wacom drivers have on windows on linux. Keycode 330 is not respected or used by any program I've seen.
If you know a better way, please tell me. (Temp-switching is not good enough for my sporadic drawing workflow. It must stay one-handed. One-handed actually works fine with CSP with CTRL as the eraser, until you start hitting CTRL + Z...)

Requires Xdotool, or Ydotool if you're using the old 21 version.
Also requires that you use CTRL to erase on clip studio paint, in your brush's hotkey/global binds or whatever.
