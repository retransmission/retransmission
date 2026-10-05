Retransmission is fundamentally a BitTorrent client and communicates with other BitTorrent peers.
In addition to this Retransmission supports network-based remote control, whereby an authorised user may control the Retransmission core from the same or another machine via the [Retransmission JSON RPC protocol](rpc-spec.md).
To make remote control easier from an arbitrary machine, Retransmission core can also serve a JavaScript web application to any browser and which in turn makes JSON RPC calls back to the Retransmission core.

The core components and methods of control are shown below:

![Architecture](https://transmission.github.io/wiki-images/Transmission_Architecture.gif)

From the above diagram it can be seen that a Retransmission core may be controlled by the following:
 * _Local_ directly linked GUI (macOS, GTK+)
 * _Local_ or _remote_ Qt GUI
 * _Local_ or _remote_ command line utility
 * _Local_ or _remote_ Retransmission web application running in a web browser

The multiple methods of controlling Retransmission and the various native GUIs available result in several different Retransmission products as shown in the figure below:

![Products](https://transmission.github.io/wiki-images/Transmission_Products.gif)

The products are:
 * Retransmission desktop - macOS
 * Retransmission desktop - Windows, Linux/Qt
 * Retransmission desktop - Linux/GTK+
 * Retransmission daemon (headless)
 * Retransmission command line

The Retransmission packages available on various distributions may include one or more of these components.
Note. Although the diagram shows "Transmission Desktop Qt" as being a Qt GUI with Retransmission core, the Qt component may be packaged on its own as a purely remote tool.
