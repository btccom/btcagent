package main

import (
	"testing"

	"github.com/bits-and-blooms/bitset"
)

func TestSessionIDManager16Bits(t *testing.T) {
	m, err := NewSessionIDManager(0xff, 16)
	if err != nil {
		t.Errorf("NewSessionIDManager return an error: %s", err.Error())
		return
	}

	beginIndex := m.allocIDx

	// fill all session ids
	{
		var i uint32
		for i = 0; i <= 0x0000ffff; i++ {
			var id1, id2 uint32
			index := (i + beginIndex) & 0x0000ffff
			id1 = (0xff << 16) | index
			id2, err := m.AllocSessionID()
			if err != nil {
				t.Errorf("AllocSessionID return an error: %s", err.Error())
				return
			}
			if id1 != id2 {
				t.Errorf("checking AllocSessionID failed, expected ID: %x, returned ID: %x", id1, id2)
				return
			}
		}
		id, err := m.AllocSessionID()
		if err == nil {
			t.Errorf("AllocSessionID should return an error because session ID is full, but it returned a session ID: %x", id)
			return
		}
	}

	// free the first one
	{
		var id1, id2 uint32
		id1 = 0x00ff0000
		m.FreeSessionID(id1)
		id2, err := m.AllocSessionID()
		if err != nil {
			t.Errorf("AllocSessionID return an error: %s", err.Error())
			return
		}
		if id1 != id2 {
			t.Errorf("checking FreeSessionID and AllocSessionID failed, expected ID: %x, returned ID: %x", id1, id2)
			return
		}
	}

	// free the second one
	{
		var id1, id2 uint32
		id1 = 0x00ff0001
		m.FreeSessionID(id1)
		id2, err := m.AllocSessionID()
		if err != nil {
			t.Errorf("AllocSessionID return an error: %s", err.Error())
			return
		}
		if id1 != id2 {
			t.Errorf("checking FreeSessionID and AllocSessionID failed, expected ID: %x, returned ID: %x", id1, id2)
			return
		}
	}

	// free the one in the middle
	{
		var id1, id2 uint32
		id1 = 0x00ff5024
		m.FreeSessionID(id1)
		id2, err := m.AllocSessionID()
		if err != nil {
			t.Errorf("AllocSessionID return an error: %s", err.Error())
			return
		}
		if id1 != id2 {
			t.Errorf("checking FreeSessionID and AllocSessionID failed, expected ID: %x, returned ID: %x", id1, id2)
			return
		}
	}

	// free the penult one
	{
		var id1, id2 uint32
		id1 = 0x00fffffe
		m.FreeSessionID(id1)
		id2, err := m.AllocSessionID()
		if err != nil {
			t.Errorf("AllocSessionID return an error: %s", err.Error())
			return
		}
		if id1 != id2 {
			t.Errorf("checking FreeSessionID and AllocSessionID failed, expected ID: %x, returned ID: %x", id1, id2)
			return
		}
	}

	// free the last one
	{
		var id1, id2 uint32
		id1 = 0x00ffffff
		m.FreeSessionID(id1)
		id2, err := m.AllocSessionID()
		if err != nil {
			t.Errorf("AllocSessionID return an error: %s", err.Error())
			return
		}
		if id1 != id2 {
			t.Errorf("checking FreeSessionID and AllocSessionID failed, expected ID: %x, returned ID: %x", id1, id2)
			return
		}
	}

	// free all ids
	{
		var i uint32
		for i = 0x00ff0000; i <= 0x00ffffff; i++ {
			m.FreeSessionID(i)
		}
		if m.count != 0 {
			t.Errorf("checking FreeSessionID failed, m.count should be 0 after free all ids, but it is %d", m.count)
			return
		}
	}
}

func TestSessionIDManager16BitsWithInterval(t *testing.T) {
	m, err := NewSessionIDManager(0xff, 16)
	if err != nil {
		t.Errorf("NewSessionIDManager return an error: %s", err.Error())
		return
	}

	// repeat 10 times
	for roll := 0; roll < 10; roll++ {

		sessionIDs := bitset.New(uint(0x100000000))

		// fill all session ids
		{
			var i uint32
			for i = 0; i <= 0x0000ffff; i++ {
				var id uint32
				id, err := m.AllocSessionID()
				if err != nil {
					t.Errorf("AllocSessionID return an error: %s", err.Error())
					return
				}
				if sessionIDs.Test(uint(id)) {
					t.Errorf("AllocSessionID return a duplicated id: %x", id)
					return
				}
				sessionIDs.Set(uint(id))
			}
			id, err := m.AllocSessionID()
			if err == nil {
				t.Errorf("AllocSessionID should return an error because session ID is full, but it returned a session ID: %x", id)
				return
			}
		}

		// free the first one
		{
			var id1, id2 uint32
			id1 = 0x00ff0000
			m.FreeSessionID(id1)
			id2, err := m.AllocSessionID()
			if err != nil {
				t.Errorf("AllocSessionID return an error: %s", err.Error())
				return
			}
			if id1 != id2 {
				t.Errorf("checking FreeSessionID and AllocSessionID failed, expected ID: %x, returned ID: %x", id1, id2)
				return
			}
		}

		// free the second one
		{
			var id1, id2 uint32
			id1 = 0x00ff0001
			m.FreeSessionID(id1)
			id2, err := m.AllocSessionID()
			if err != nil {
				t.Errorf("AllocSessionID return an error: %s", err.Error())
				return
			}
			if id1 != id2 {
				t.Errorf("checking FreeSessionID and AllocSessionID failed, expected ID: %x, returned ID: %x", id1, id2)
				return
			}
		}

		// free the one in the middle
		{
			var id1, id2 uint32
			id1 = 0x00ff5024
			m.FreeSessionID(id1)
			id2, err := m.AllocSessionID()
			if err != nil {
				t.Errorf("AllocSessionID return an error: %s", err.Error())
				return
			}
			if id1 != id2 {
				t.Errorf("checking FreeSessionID and AllocSessionID failed, expected ID: %x, returned ID: %x", id1, id2)
				return
			}
		}

		// free the penult one
		{
			var id1, id2 uint32
			id1 = 0x00fffffe
			m.FreeSessionID(id1)
			id2, err := m.AllocSessionID()
			if err != nil {
				t.Errorf("AllocSessionID return an error: %s", err.Error())
				return
			}
			if id1 != id2 {
				t.Errorf("checking FreeSessionID and AllocSessionID failed, expected ID: %x, returned ID: %x", id1, id2)
				return
			}
		}

		// free the last one
		{
			var id1, id2 uint32
			id1 = 0x00ffffff
			m.FreeSessionID(id1)
			id2, err := m.AllocSessionID()
			if err != nil {
				t.Errorf("AllocSessionID return an error: %s", err.Error())
				return
			}
			if id1 != id2 {
				t.Errorf("checking FreeSessionID and AllocSessionID failed, expected ID: %x, returned ID: %x", id1, id2)
				return
			}
		}

		// free all ids
		{
			var i uint32
			for i = 0x00ff0000; i <= 0x00ffffff; i++ {
				m.FreeSessionID(i)
			}
			if m.count != 0 {
				t.Errorf("checking FreeSessionID failed, m.count should be 0 after free all ids, but it is %d", m.count)
				return
			}
		}
	}
}
