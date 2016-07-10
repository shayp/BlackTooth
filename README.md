# BlackTooth
BlackTooth is a set of tools to scan and attack Bluetooth devices.
## scanner
Scripts for scanning in real time
* CGI/
    *   cgi-devscan.cpp - device scan code
    *   refresh_apache.sh - Run this in order to refresh the server and before you run the code
## HTML 
Include all the HTML & JavaScript Code for the scanner UI
* HTML/ 
    
    * Index.html - the html page of the scan
    * scripts/ - all the JavaScript scripts: Maps, images, mac resolve etc'
## Dual pair MITM
Include the MITM code for sniffing and attacking audio & telephone bluetooth protocols.
The attack works for both audio & telephone.
* dual_pair/
    * restart.sh - compile and run the MITM sniifer & Attacker
    * dual_pair.c - The MITM code for sniffing and attacking
## Setup
* scanning devices
    * run ./CGI/refresh_apache.sh
    * open your browswer at 127.0.0.1 :)
* Dual pair MITM
    * Update  dual_pair/dual_pair.c
        * dest1_mac - the headset/speaker mac
        * dest2_mac - the mobile phone mac
        * local_mac - the MITM attacker mac(our mac)
        *  rfcomm_channel1, rfcomm_channel2 -   Update to the device const value
    * Run restart.sh :)
## Authors
* Amir Barak
* Shay Perchik