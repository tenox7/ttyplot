package main

import (
	"container/ring"
	"fmt"
	"io"
	"sync"
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

func (d *data) dump() {
	d.Lock()
	defer d.Unlock()
	fmt.Print("[ ")
	d.r.Do(func(p any) {
		fmt.Print(p, " ")
	})
	fmt.Println(" ]")
}

func main() {
	d := newData(20)

	for {
		var l float64
		n, err := fmt.Scan(&l)
		if err != nil || n == 0 {
			if err == io.EOF {
				break
			}
			continue
		}
		d.push(l)
		d.dump()
	}
}
