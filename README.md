# Overview:

![block_diagram](https://user-images.githubusercontent.com/26395394/111260593-5a59ca80-85de-11eb-9ad7-3fb81c4b7f3a.png)

wiz_switch is an ESP-IDF based project intended to control a list of wiz "smart" light bulbs. When the board loses 5v power, it turns the "paired" bulbs off. When it gains 5v power, it turns the "paired" bulbs on.

My hardware uses a [tinypico board from unexpected maker](https://www.tinypico.com/), with a simple voltage divider circuit added on to detect the loss of 5v (ie USB power). The board is powered by a small lipo battery and then is plugged into a USB AC adapter that is plugged into a switched AC outlet. Turning the switch on powers the 5v USB and triggers the wiz message transmission

# Use Case:
The lightswitch in my bedroom does not control any hardwired fixtures, but rather a single switched outlet near the door. The outlet is in a spot where having a floor lamp or other fixture would be inconvenient, so the lamps in the room are plugged into other outlets. I would like the lightswitch to control these lights, so I put together this project to control the desired lights. The current iteration is hard coded to the MAC addresses of the bulbs I would like to control, but future iterations will have an improved setup experience.

![switch_on](https://user-images.githubusercontent.com/26395394/111260669-7c534d00-85de-11eb-8cd5-b99d08be2b09.png)

![switch_off](https://user-images.githubusercontent.com/26395394/111260682-807f6a80-85de-11eb-86b8-a97dd5a615a3.png)


# TODOs:
Software:
-Put board into deep sleep mode after sending the OFF payload in order to maximize battery life
-Implement "setup mode" via soft AP or some other method to select which bulbs you want to "pair" and control via the switch
-Use setup mode to identify bulbs, store them with a "name" etc. 
-Move beyond the example espidf wifi connection code

Hardware:
-Solder down board to perma-proto board with voltage divider circuit
-Figure out a small enclosure to affix to wallwart
