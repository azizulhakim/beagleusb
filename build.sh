#!/bin/bash

 #	Beaglebone USB Driver for Android Device Using AOA v2.0 Protocol
 #
 #   Copyright (C) 2014  Azizul Hakim
 #   azizulfahim2002@gmail.com
 #
 #   This program is free software: you can redistribute it and/or modify
 #   it under the terms of the GNU General Public License as published by
 #   the Free Software Foundation, either version 3 of the License, or
 #   (at your option) any later version.
 #
 #   This program is distributed in the hope that it will be useful,
 #   but WITHOUT ANY WARRANTY; without even the implied warranty of
 #   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 #   GNU General Public License for more details.
 #
 #   You should have received a copy of the GNU General Public License
 #   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 #

if [ "$1" = "help" ]; then
	echo "####################################################################"
	echo "#  make - builds the binary                                        #"
	echo "#  clean - clean up the built binaries                             #"
	echo "#  install - installs the built drivers in your system             #"
	echo "#  uninstall - uninstalls the installed drivers from your system   #"
	echo "#  run - run X on the installed framebuffervers from your system   #"
	echo "####################################################################"
	printf "\n\n"

elif [ "$1" = "make" ]; then
	make clean
	echo "####################################################################"
	echo "######################### Build ADK Driver #########################"
	echo "####################################################################"

	cd adk
	make clean
	make
	cd ..

	echo "####################################################################"
	echo "###################### Build beagleusb Driver ######################"
	echo "####################################################################"

	make

	echo "####################################################################"
	if [ -e "adk/adk.ko" ]; then
		echo "################### adk driver build successful ####################"
	else
		echo "################## adk driver build unsuccessful ###################"
	fi

	if [ -e "beagle.ko" ]; then
		echo "################ beagleusb driver build successful #################"
	else
		echo "############### beagleusb driver build unsuccessful ################"
	fi
	echo "####################################################################"

elif [ "$1" = "clean" ]; then
	cd adk
	make clean
	cd ..
	make clean

elif [ "$1" = "install" ]; then
	insmod adk/adk.ko
	insmod beagle.ko

elif [ "$1" = "uninstall" ]; then
	rmmod adk.ko
	rmmod beagle.ko

elif [ "$1" = "run" ]; then
	FRAMEBUFFER=/dev/fb1 startx -- /usr/bin/X :1

fi
