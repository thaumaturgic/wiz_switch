# Overview:

wiz_switch is an ESP-IDF based project intended to control a list of wiz "smart" light bulbs. When the board loses 5v power, it turns the "paired" bulbs off. When it gains 5v power, it turns the "paired" bulbs on.

My hardware uses a [tinypico board from unexpected maker](https://www.tinypico.com/), with a simple voltage divider circuit added on to detect the loss of 5v (ie USB power). The board is powered by a small lipo battery and then is plugged into a USB AC adapter that is plugged into a switched AC outlet. Turning the switch on powers the 5v USB and triggers the wiz message transmission

# Use Case:
The lightswitch in my bedroom does not control any hardwired fixtures, but rather a single switched outlet near the door. The outlet is in a spot where having a floor lamp or other fixture would be inconvenient, so the lamps in the room are plugged into other outlets. I would like the lightswitch to control these lights, so I put together this project to control the desired lights. The current iteration is hard coded to the MAC addresses of the bulbs I would like to control, but future iterations will have an improved setup experience.