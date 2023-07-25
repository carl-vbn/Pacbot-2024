package main

import (
	"fmt"
	"log"
	"net/http"
	"pacbot_server/clock"
	"pacbot_server/game"
	"pacbot_server/tcpserver"
	"pacbot_server/webserver"
	"time"

	"github.com/loov/hrtime"
)

func main() {

	// Get the configuration info (config.go)
	conf := GetConfig()

	// Make channels for communication between web broker and game engine
	webBroadcastCh := make(chan []byte, 10)
	webResponseCh := make(chan []byte, 10)

	// Websocket stuff (webserver)
	webserver.ConfigOneBrowserPerIP(conf.OneBrowserPerIP)
	webserver.ConfigTrustedBrowserIPs(conf.TrustedBrowserIPs)
	wb := webserver.NewWebBroker(webBroadcastCh, webResponseCh)
	go wb.RunLoop()
	http.HandleFunc("/", webserver.WebSocketHandler)
	go http.ListenAndServe(fmt.Sprintf(":%d", conf.WebSocketPort), nil)

	// TCP stuff (tcp_server.go)
	server := tcpserver.NewTcpServer(fmt.Sprintf(":%d", conf.TcpPort))
	go server.Printer()

	// High-resolution ticker (for keeping the frame rate roughly constant)
	hrt := clock.NewHighResTicker(24)

	/*
		Demo for high-resolution ticker - at the specified FPS, it updates the web
		broker's send channel with the time (in seconds) and frames elapsed for
		that second - suffers less lag compared to timer.Ticker() on Windows
	*/
	go func(wb *webserver.WebBroker) {
		go hrt.Start()
		for idx := 0; idx < 5000; idx++ {
			select {
			case webBroadcastCh <- game.SerializePellets(game.Pellets):
				game.Pellets[0] += 1 // Test reactivity of Svelte frontend
			default:
			}
			if wb.HasQuit() {
				fmt.Println("fast:", hrt.Lifetime())
				fmt.Println("msg cnt:", idx)
				return
			}
			<-hrt.ReadyCh
		}
	}(wb)

	/*
		Demo for time-keeping abilities: after 60s, all websockets will be killed through
		quitting the broker, freezing the time received on the web client
	*/
	go func(wb *webserver.WebBroker) {
		start := hrtime.Now()
		time.Sleep(10 * time.Second)
		wb.Quit()
		fmt.Println("slow:", hrtime.Since(start))
		wb.Quit()
	}(wb)

	// Log TCP errors
	log.Fatal(server.TcpStart())
}