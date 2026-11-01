# Debugging

To debug each individual board and test data transfer over LAN using your PC.

## PuTTY

To view all logs (printk and more) that the responder or requester will make you can use PuTTY.

1. Install PuTTY and start it.

2. Set *Connection Type* to Serial. Then on the Category navigate to Serial. Enter the following options:

    - Enter the COM port the board is connected to.
    - Speed (baud): 115400
    - Data bits: 8
    - Stop bits: 1
    - Parity: None
    - Flow control: None

3. Check your options and press Open. Now you should see all Logging done by the requester or the responder.

## Wireshark and Ethernet

To view the data transfer (over ethernet) between the board and the pc (therefore testing functionality) you can use Wireshark. Make sure to take the following steps first:

1. Connect the board and your pc via an ethernet cable
2. Set the Ethernet IP Address manually for your pc (follow any online guide on how to do that).
    - Submask should always be `255.255.255.0`
    - To test the requester set the IP Address to `192.168.1.11` as you are simulating the responder
    - To test the responder set the IP Adress to `192.168.1.10` as you are simulating the requester
3. Start Wireshark (install it if not already installed)
4. Go to Ethernet and filter by `udp.port == 17220`

Once you flash the requester to the board you should see messages coming in. For the responder you will need to send a UDP packet to it first! You can do that via terminal or any other method that suits you. Once you send a message an answer should return.