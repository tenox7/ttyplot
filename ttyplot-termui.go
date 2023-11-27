// ttyplot-go-termui
package main

import (
	"container/ring"
	"fmt"
	"io"
	"log"
	"os"
	"strings"
	"sync"
	"time"

	ui "github.com/gizak/termui/v3"
	"github.com/gizak/termui/v3/widgets"
)

type data struct {
	r *ring.Ring
	sync.Mutex
}

func newData(n int) *data {
	d := new(data)
	d.r = ring.New(n)
	return d
}

func (d *data) push(l float64) {
	d.Lock()
	defer d.Unlock()
	d.r.Value = l
	d.r = d.r.Next()
}

func (d *data) string() string {
	d.Lock()
	defer d.Unlock()
	s := strings.Builder{}
	fmt.Fprint(&s, "[")
	d.r.Do(func(p any) {
		fmt.Fprint(&s, p, " ")
	})
	fmt.Fprint(&s, " ]")
	return s.String()
}

func (d *data) slice() (s []float64) {
	d.Lock()
	defer d.Unlock()
	d.r.Do(func(p any) {
		if p == nil {
			s = append(s, 0)
			return
		}
		s = append(s, p.(float64))
	})
	return
}

func readStdin(d *data) {
	for {
		var l float64
		n, err := fmt.Scan(&l)
		if err != nil || n == 0 {
			os.Exit(0)
			if err == io.EOF {
			}
			continue
		}
		d.push(l)
	}
}

func main() {
	err := ui.Init()
	if err != nil {
		log.Fatalf("failed to initialize termui: %v", err)
	}
	defer ui.Close()

	p := widgets.NewParagraph()
	p.Title = "Dump"
	p.SetRect(0, 0, 60, 5)

	l := widgets.NewSparkline()
	l.LineColor = ui.ColorRed
	l.TitleStyle.Fg = ui.ColorGreen
	lg := widgets.NewSparklineGroup(l)
	lg.SetRect(0, 15, 20, 5)
	lg.Title = "Sparkline"

	d := newData(20)
	go readStdin(d)

	uiEvents := ui.PollEvents()
	ticker := time.NewTicker(time.Second).C
	for {
		select {
		case e := <-uiEvents:
			switch e.ID {
			case "q", "<C-c>":
				return
			}
		case <-ticker:
			p.Text = d.string()
			l.Data = d.slice()
			ui.Render(p, lg)
		}
	}
}
