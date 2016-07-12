# BlackTooth

BlackTooth is a set of tools to scan and attack Bluetooth devices. It is comprised of probing tools and a CGI based website that allows the monitoring of nearby Bluetooth devices. The tools are based on the Linux Bluetooth stack **BlueZ v4**. BlueZ v5 is *not* supported!

## Scanner
Scripts for scanning and detecting nearby devices in real time
* CGI/

    *   cgi-devscan.cpp - CGI device scan and probe source code
    *   refresh_apache.sh - Run this in order to refresh the server with the most up to date binaries

## HTML 
Includes all the HTML & JavaScript code for the scanner website UI
* HTML/ 
    
    * index.html - the HTML source for the website UI
    * scripts/ - all the JavaScript scripts: Maps, images, mac resolve etc

## Dual pair MITM
Includes the code that performs the MITM attack, used for sniffing and attacking audio & telephony Bluetooth traffic & protocols.
The attack works for both audio & telephony.
* dual_pair/

    * restart.sh - recompiles the source and runs the MITM sniffer & attacker
    * dual_pair.c - The source code for the MITM attack

## Setup
* scanning devices
    * Follow the instructions in ./CGI/setting_up_apache.txt to configure your apache web server
    * run ./CGI/refresh_apache.sh to refresh the CGI binaries and HTML page with the latest ones
    * open your browser at 127.0.0.1 :)
* Dual pair MITM
    * Update  dual\_pair/dual\_pair.c to target your wanted devices and configure local controller
        * dest1\_mac - the headset/speaker (*audio sink*) mac
        * dest2\_mac - the mobile phone (*audio gateway*) mac
        * local\_mac - the MITM attacker mac (our **local** controller's mac)
        *  rfcomm\_channel1, rfcomm\_channel2 -   Update to the device const value
            * You can discover these by uncommenting the two sdp\_lookup\_uuid\_rfcomm\_channel() function calls and supplying the wanted service UUID
            * This will query the target MACs for the RFCOMM channel of the specified UUID service via SDP
            * The relevant ones are defined in the code already, replace as necessary
            * **Do not run the MITM attack straight after an SDP query!** Do so in two different executions of the dual\_pair binary
    * Disable the bluetooth service to allow service traffic to reach dual\_pair: *sudo service bluetooth stop*
    * Run restart.sh :)

## Authors
* Amir Barak
* Shay Perchik