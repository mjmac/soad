//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package events

import (
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/mjmac/soad/src/control/common"
)

func TestEvents_ConvertRankDown(t *testing.T) {
	event := NewRankDownEvent("foo", 1, 1, common.ExitStatus("test"))

	pbEvent, err := event.ToProto()
	if err != nil {
		t.Fatal(err)
	}

	t.Logf("proto event: %+v (%T)", pbEvent, pbEvent)

	returnedEvent := new(RASEvent)
	if err := returnedEvent.FromProto(pbEvent); err != nil {
		t.Fatal(err)
	}

	t.Logf("native event: %+v, %+v", returnedEvent, returnedEvent.ExtendedInfo)

	if diff := cmp.Diff(event, returnedEvent, defEvtCmpOpts...); diff != "" {
		t.Fatalf("unexpected event (-want, +got):\n%s\n", diff)
	}
}
