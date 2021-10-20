package main

import (
	"testing"
)

func TestSessionIDManager16Bits(t *testing.T) {
	m, err := NewSessionIDManager(0xfffe)
	if err != nil {
		t.Errorf("NewSessionIDManager return an error: %s", err.Error())
		return
	}

	// fill all session ids
	{
		var i uint32
		for i = 0; i <= 0xfffe; i++ {
			id, err := m.AllocSessionID()
			if err != nil {
				t.Errorf("AllocSessionID return an error: %s", err.Error())
				return
			}
			if i != id {
				t.Errorf("checking AllocSessionID failed, expected ID: %x, returned ID: %x", i, id)
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
		id1 = 0x0000
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
		id1 = 0x0001
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
		id1 = 0x5024
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
		id1 = 0xfffd
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
		id1 = 0x00fffe
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
		for i = 0x0000; i <= 0xfffe; i++ {
			m.FreeSessionID(i)
		}
		if m.count != 0 {
			t.Errorf("checking FreeSessionID failed, m.count should be 0 after free all ids, but it is %d", m.count)
			return
		}
	}
}
