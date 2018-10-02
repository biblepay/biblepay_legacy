Installing IPFS on Ubuntu64:


wget https://dist.ipfs.io/ipfs-update/v1.5.2/ipfs-update_v1.5.2_linux-amd64.tar.gz
tar xvfz ip*.tar.gz
./ipfs-update/install.sh
/usr/local/bin/ipfs-update/ipfs-update install latest

Start for first time:
ipfs init
ipfs daemon


Note: For Biblepay integration with sanctuaries please follow these instructions after installing IPFS:
Edit IPFS config to expose IPFS gateway for Biblepay integration:

cd ~/.ipfs
nano config
scroll down to line 47 (the line reads:  "Gateway:" /ip4/127.0.0.1/tcp/8080)

Edit the line to be:
"Gateway": "/ip4/your_sanc_public_ip/tcp/8080"



Running IPFS on your Sanc:

After each machine reboot start IPFS (IPFS is already in users path):

ipfs daemon




