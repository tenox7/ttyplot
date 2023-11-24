package main

import (
	"container/ring"
	"fmt"
	"io"
)

func main() {
	r := ring.New(20)
	for {
		var l float64
		n, err := fmt.Scan(&l)
		if err != nil || n == 0 {
			if err == io.EOF {
				break
			}
			continue
		}
		r.Value = l
		r = r.Next()
		fmt.Print("[ ")
		r.Do(func(p any) {
			fmt.Print(p, " ")
		})
		fmt.Println(" ]")
	}
}
