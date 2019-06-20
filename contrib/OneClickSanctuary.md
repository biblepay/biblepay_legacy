***** BIBLEPAY EVOLUTION  --  ONE-CLICK SANCTUARY INSTALLATION SCRIPT FOR VMS OR HOME USE ****** 


URL:
https://raw.githubusercontent.com/biblepay/biblepay-evolution/master/contrib/masternode-install.sh

Command line script options

-u  Run unattended with defaults (upgrade if .biblepayevolution found, clean install if not)
-n  Don't run apt-get update/upgrade/dist-upgrade/remove 
-s  Swap size (1G by default). Size values not validated.

Instructions:
1.  Lease a VMS (for example the Vultr Ubuntu 18/64) 
2.  Download the script to the VMS:
     cd ~
     wget URL

3.  Ensure script is executable:
     chmod 777 masternode-install.sh

4.  Run the script:
     ./masternode-install.sh

Follow the prompts.  The script will ask you if you would like to compile from source or binary.

5.  Copy the Sanctuary private key to the controller wallet masternode.conf file, and finish configuration of the Sanctuary.

All credit goes to MIP for creating the script.   And of course to Yeshua for maintaining all of BiblePay.
