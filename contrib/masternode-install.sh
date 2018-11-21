#!/usr/bin/env bash
NONE='\033[00m'
RED='\033[01;31m'
GREEN='\033[01;32m'
YELLOW='\033[01;33m'
PURPLE='\033[01;35m'
CYAN='\033[01;36m'
WHITE='\033[01;37m'
BOLD='\033[1m'
UNDERLINE='\033[4m'
MAX=11
CURRSTEP=0

COINDOWNLOADLINK=https://github.com/biblepay/biblepay/releases/download/biblepay-aarch64-linux-gnu.tar.gz
COINDOWNLOADFILE=biblepay-aarch64-linux-gnu.tar.gz
COINREPO=https://github.com/biblepay/biblepay.git
COINRPCPORT=9998
COINPORT=40000
COINDAEMON=biblepayd
COINCLIENT=biblepay-cli
COINTX=biblepay-tx
COINCORE=.biblepaycore
COINCONFIG=biblepay.conf
COINDOWNLOADDIR=biblepay


purgeOldInstallation() {
    echo "Searching and removing old masternode files and configurations"
    #kill wallet daemon
    sudo killall biblepayd > /dev/null 2>&1
    #remove old ufw port allow
    sudo ufw delete allow COINRPCPORT/tcp > /dev/null 2>&1
    sudo ufw delete allow COINPORT/tcp > /dev/null 2>&1
    #remove old files
    if [ -d "~/.biblepaycore" ]; then
        sudo rm -rf ~/.biblepaycore > /dev/null 2>&1
    fi
    #remove binaries and biblepay utilities
    cd /usr/local/bin && sudo rm biblepay-cli biblepay-tx biblepayd biblepay-qt> /dev/null 2>&1 && cd
    echo -e "${GREEN}* Done${NONE}";
}

checkForUbuntuVersion() {
   let "CURRSTEP++"
   echo
   echo "[${CURRSTEP}/${MAX}] Checking Ubuntu version..."
    if [[ `cat /etc/issue.net`  == *16.04* ]] || [[ `cat /etc/issue.net`  == *18.04* ]]; then
        echo -e "${GREEN}* You are running `cat /etc/issue.net` . Setup will continue.${NONE}";
    else
        echo -e "${RED}* You are not running Ubuntu 16.04.X or 18.04.X. You are running `cat /etc/issue.net` ${NONE}";
        echo && echo "Installation cancelled" && echo;
        exit;
    fi
}

updateAndUpgrade() {
    let "CURRSTEP++"
    echo
    echo "[${CURRSTEP}/${MAX}] Running update and upgrade. Please wait..."
    sudo DEBIAN_FRONTEND=noninteractive apt-get update -qq -y > /dev/null 2>&1
    sudo DEBIAN_FRONTEND=noninteractive apt-get upgrade -y -qq > /dev/null 2>&1
    echo -e "${GREEN}* Done${NONE}";
}

setupSwap() {
    swapspace=$(free -h | grep Swap | cut -c 16-18);
    if [ $(echo "$swapspace < 1.0" | bc) -ne 0 ]; then

    echo a; else echo b; fi

    echo -e "${BOLD}"
    read -e -p "Add swap space? (Recommended for VPS that have 1GB of RAM) [Y/n] :" add_swap
    if [[ ("$add_swap" == "y" || "$add_swap" == "Y" || "$add_swap" == "") ]]; then
        swap_size="4G"
    else
        echo -e "${NONE}[3/${MAX}] Swap space not created."
    fi

    if [[ ("$add_swap" == "y" || "$add_swap" == "Y" || "$add_swap" == "") ]]; then
        echo && echo -e "${NONE}[3/${MAX}] Adding swap space...${YELLOW}"
        sudo fallocate -l $swap_size /swapfile
        sleep 2
        sudo chmod 600 /swapfile
        sudo mkswap /swapfile
        sudo swapon /swapfile
        echo -e "/swapfile none swap sw 0 0" | sudo tee -a /etc/fstab > /dev/null 2>&1
        sudo sysctl vm.swappiness=10
        sudo sysctl vm.vfs_cache_pressure=50
        echo -e "vm.swappiness=10" | sudo tee -a /etc/sysctl.conf > /dev/null 2>&1
        echo -e "vm.vfs_cache_pressure=50" | sudo tee -a /etc/sysctl.conf > /dev/null 2>&1
        echo -e "${NONE}${GREEN}* Done${NONE}";
    fi
}

installFirewall() {
    let "CURRSTEP++"
    echo
    echo -e "[${CURRSTEP}/${MAX}] Installing UFW Firewall and opening Biblepay port. Please wait..."
    sudo apt-get -y install ufw > /dev/null 2>&1
    sudo ufw allow ssh/tcp > /dev/null 2>&1
    sudo ufw limit ssh/tcp > /dev/null 2>&1
    sudo ufw allow $COINPORT/tcp > /dev/null 2>&1
    sudo ufw allow $COINRPCPORT/tcp > /dev/null 2>&1
    echo "y" | sudo ufw enable > /dev/null 2>&1
    echo -e "${NONE}${GREEN}* Done${NONE}";
}

installDependencies() {
    let "CURRSTEP++"
    echo
    echo -e "[${CURRSTEP}/${MAX}] Installing dependencies. Please wait..."
    sudo add-apt-repository main > /dev/null 2>&1
    sudo add-apt-repository universe > /dev/null 2>&1  # missing in clean Ubuntu 18.04
    sudo apt-get install bc git nano rpl wget python-virtualenv -qq -y > /dev/null 2>&1
    sudo apt-get install curl build-essential libtool automake autoconf -qq -y > /dev/null 2>&1
    sudo apt-get install autotools-dev autoconf pkg-config libssl-dev -qq -y > /dev/null 2>&1
    sudo apt-get install libgmp3-dev libevent-dev bsdmainutils libboost-all-dev -qq -y > /dev/null 2>&1
    sudo apt-get install software-properties-common python-software-properties -qq -y > /dev/null 2>&1
    sudo add-apt-repository ppa:bitcoin/bitcoin -y > /dev/null 2>&1
    sudo apt-get update -qq -y > /dev/null 2>&1
    sudo apt-get install libdb4.8-dev libdb4.8++-dev -qq -y > /dev/null 2>&1
    sudo apt-get install libminiupnpc-dev -qq -y > /dev/null 2>&1
    sudo apt-get install libzmq5 -qq -y > /dev/null 2>&1
    sudo apt-get install virtualenv -qq -y > /dev/null 2>&1
    echo -e "${NONE}${GREEN}* Done${NONE}";
}

downloadWallet() {
    let "CURRSTEP++"
    let "CURRSTEP++"
    let "CURRSTEP++"
    echo
    echo -e "[${CURRSTEP}/${MAX}] Downloading wallet binaries. Please wait, this might take a while to complete..."
    sudo add-apt-repository ppa:biblepay-official/mainnet -y > /dev/null 2>&1
    sudo apt-get update -y > /dev/null 2>&1
    sudo apt-get install biblepayd -qq -y > /dev/null 2>&1
    echo -e "${NONE}${GREEN}* Done${NONE}";
}

compileWallet() {
    let "CURRSTEP++"
    echo
    echo -e "${NONE}${BOLD}${PURPLE}[${CURRSTEP}/${MAX}] Compiling wallet. This can take a while.  Feel free to grab some coffee...and maybe watch a movie...${NONE}"
    cd && mkdir -p $COINDOWNLOADDIR
    git clone $COINREPO $COINDOWNLOADDIR >/dev/null 2>&1
    cd $COINDOWNLOADDIR
    sudo chmod 777 share/genbuild.sh  
    sudo chmod 777 autogen.sh  
    ./autogen.sh > /dev/null 2>&1
    ./configure > /dev/null 2>&1
    cd src
    make -j$(nproc) > /dev/null 2>&1
    echo -e "${NONE}${GREEN}* Done${NONE}"
}

installWallet() {
    let "CURRSTEP++"
    echo
    echo -e "[${CURRSTEP}/${MAX}] Installing wallet. One moment, please..."
    strip $COINDAEMON  > /dev/null 2>&1
    strip $COINCLIENT > /dev/null 2>&1
    strip $COINTX > /dev/null 2>&1
    sudo make install  > /dev/null 2>&1
    cd
    let "CURRSTEP++"
    echo -e "${NONE}${GREEN}* Done${NONE}";
}

configureWallet() {
    let "CURRSTEP++"
    echo
    echo -e "[${CURRSTEP}/${MAX}] Configuring wallet. One moment..."
    $COINDAEMON -daemon > /dev/null 2>&1
    sleep 20
    $COINCLIENT stop > /dev/null 2>&1
    sleep 2
    mnip=$(curl --silent ipinfo.io/ip)
    rpcuser=`cat /dev/urandom | tr -dc 'a-zA-Z0-9' | fold -w 32 | head -n 1`
    rpcpass=`cat /dev/urandom | tr -dc 'a-zA-Z0-9' | fold -w 32 | head -n 1`
    echo -e "rpcuser=${rpcuser}\nrpcpassword=${rpcpass}\nrpcallowedip=127.0.0.1\nlisten=1\nserver=1\ndaemon=1" > ~/$COINCORE/$COINCONFIG
    $COINDAEMON -daemon > /dev/null 2>&1
    sleep 20
    mnkey=$($COINCLIENT masternode genkey)
    $COINCLIENT stop > /dev/null 2>&1
    sleep 2
    echo -e "rpcuser=${rpcuser}\nrpcpassword=${rpcpass}\nrpcport=${COINRPCPORT}\nrpcallowip=127.0.0.1\nlogtimestamps=1\ndaemon=1\nserver=1\nlisten=1\nmasternode=1\nexternalip=${mnip}\nmaxconnections=256\nmasternodeprivkey=${mnkey}\n" > ~/$COINCORE/$COINCONFIG
    echo -e "${NONE}${GREEN}* Done${NONE}";
}

startWallet() {
    let "CURRSTEP++"
    echo
    echo -e "[${CURRSTEP}/${MAX}] Starting wallet daemon..."
    $COINDAEMON -daemon > /dev/null 2>&1
    sleep 2
    echo -e "${GREEN}* Done${NONE}";
}

installWatchman() {
    let "CURRSTEP++"
    echo
    echo -e "[${CURRSTEP}/${MAX}] Installing watchman..."
    if crontab -u $USER -l|grep -q watchman.py; then 
        echo -e "watchman already present..."
    else 
        sudo apt update> /dev/null 2>&1
        cd ~/$COINCORE
        git clone https://github.com/biblepay/watchman.git> /dev/null 2>&1
        cd watchman
        virtualenv venv --python=python3> /dev/null 2>&1
        venv/bin/pip install -r requirements.txt> /dev/null 2>&1
        #write out current crontab
        crontab -u $USER -l > mycron
        echo "* * * * * cd ~/${COINCORE}/watchman && ./venv/bin/python bin/watchman.py >/dev/null 2>&1" >> mycron
        crontab mycron> /dev/null 2>&1
        rm mycron        
    fi
    
    sleep 2
    echo -e "${GREEN}* Done${NONE}";
}

cleanUp() {
    let "CURRSTEP++"
    echo
    echo -e "[${CURRSTEP}/${MAX}] Cleaning up";
    cd
    if [ -d "$COINDOWNLOADDIR" ]; then rm -rf $COINDOWNLOADDIR; fi
}

clear
cd

echo -e "--------------------------------------------------------------------"
echo -e "|                                                                   |"
echo -e "|                       ´´..-----:::----..´´                        |"
echo -e "|                   ´.-:::---:::-:::---:::-::-.´                    |"
echo -e "|               ´.-::::-:--..´.´´´´´´....----:::--.                 |"
echo -e "|             ´-:---:-.´-:+oosy´    ´yyso+:-..-:---:-´              |"
echo -e "|           .-::::-´./oyyyo.-..´    ´yyyyyyyyo/.´-::::-.            |"
echo -e "|         ´-::::.´-//:-::+--:-/´     .-:+yyyyyyys:´.::::-´          |"
echo -e "|        .::::.´´´´.-:::::--::.´     :::..+/:://-..´´.::-:.         |"
echo -e "|       -::--´:yyy/:..-::::::--´     ::::- -::--:::::.´--::-        |"
echo -e "|      -:::-´+yyyyys:´.-:---:+y´     :::::--::::::::::- -:::-       |"
echo -e "|     -:::- /ooo+:.´.--´.:+oooo´     ------------------. -:::-      |"
echo -e "|    .:::-                                                -:::.     |"
echo -e "|   ´:--:´                                                ´---:´    |"
echo -e "|   .:::- :::::::::´ ´:::::::::´     ::::::::::::::::::::: -:::.    |"
echo -e "|   -:::.-yyyyyyyyy-..-+yyyyyyy´    ´++oyyyyyyyyyyyyyyyyyy-.:::-    |"
echo -e "|   ::-:´:yyyyyyyyy.s-+::yyyyyy´     ::-./yyyyyyyyyyyyyyyy:´:--:    |"
echo -e "|   ::::´:yyyyssyy+-yy´s:-yyyyy´     ...´/syyyyyyyyyyyyyyy:´::::    |"
echo -e "|   ---:../-......´/+:´´....-/s´     ::::-.-+yyyyyyyyyyyyy-.:-:-    |"
echo -e "|   .:::- -:::::::-´-:: :::::-.      :....-:-.:+yyyyyyyyyy ---:.    |"
echo -e "|    ::-:´.::::::-´:::- .--::::´     :::::-..-:-.-/+ossyy:´::::´    |"
echo -e "|    .:::- -:::::......---...-:´     :-...-::...-:::-..yo ---:.     |"
echo -e "|     -::-- -::::.´-:::::::::.´      :--::...-::-...´+yo´--:--      |"
echo -e "|      --::- .:::´-:::::::::::-      :-...-::.....-:+y+´-:::-       |"
echo -e "|       -:::-´´--´:::::::::::::      :::::...-::-.:ys-´---:-        |"
echo -e "|        .:----´. -:::::::::::-      :::-.-::-´.+yy/´-:::-.         |"
echo -e "|         ´-::::.´´-:::::::::-.´     ::::-.´:/+yo:´.:-::-´          |"
echo -e "|           ´-:----.´.-----.-+y´     --:://oyo:..-::::-´            |"
echo -e "|             ´--:::--.´ ´-+syy´    ´yyso/:.´.-:::-:-´              |"
echo -e "|                .-:-:::::-....´´´´´´....-::--:::-.                 |"
echo -e "|                   ´.-::::::--:::::::---::::-.´                    |"
echo -e "|                                                                   |"
echo -e "|                 Biblepay Masternode Installer                     |"
echo -e "|                                                                   |"
echo -e "--------------------------------------------------------------------"
echo -e "${BOLD}"
read -p "This script will setup your Biblepay Masternode. Continue? (y/n)?" response
echo -e "${NONE}"

if [[ "$response" =~ ^([yY][eE][sS]|[yY])+$ ]]; then
echo
    purgeOldInstallation
    checkForUbuntuVersion
    updateAndUpgrade
    installFirewall
    installDependencies
    echo -e "${BOLD}"
    read -p "Use pre-compiled Biblepay binaries (y) or compile from source (n)? (y/n)?" binaries
    echo -e "${NONE}"
    if [[ "$binaries" =~ ^([yY][eE][sS]|[yY])+$ ]];
    then
      downloadWallet
    else
      setupSwap
      compileWallet
      cd ~/$COINDOWNLOADDIR/src
      installWallet
    fi
    configureWallet
    startWallet
    installWatchman
    cleanUp

    echo -e "================================================================================================"
    echo -e "${BOLD}The VPS side of your masternode has been installed. Save the masternode ip and${NONE}"
    echo -e "${BOLD}private key so you can use them to complete your local wallet part of the setup${NONE}".
    echo -e "================================================================================================"
    echo -e "${BOLD}Masternode IP:${NONE} ${mnip}:${COINPORT}"
    echo -e "${BOLD}Masternode Private Key:${NONE} ${mnkey}"
    echo -e "${BOLD}Continue with the cold wallet part of the setup${NONE}"
    echo -e "================================================================================================"
else
    echo && echo "Installation cancelled" && echo
fi
