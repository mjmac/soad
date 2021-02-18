//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"testing"

	"github.com/mjmac/soad/src/control/events"
	"github.com/mjmac/soad/src/control/lib/atm"
	"github.com/mjmac/soad/src/control/logging"
	"github.com/mjmac/soad/src/control/server/config"
	"github.com/mjmac/soad/src/control/server/engine"
	"github.com/mjmac/soad/src/control/server/storage/bdev"
	"github.com/mjmac/soad/src/control/server/storage/scm"
	"github.com/mjmac/soad/src/control/system"
)

// mockControlService takes cfgs for tuneable scm and sys provider behavior but
// default nvmeStorage behavior (cs.nvoe can be subsequently replaced in test).
func mockControlService(t *testing.T, log logging.Logger, cfg *config.Server, bmbc *bdev.MockBackendConfig, smbc *scm.MockBackendConfig, smsc *scm.MockSysConfig) *ControlService {
	t.Helper()

	if cfg == nil {
		cfg = config.DefaultServer().WithEngines(
			engine.NewConfig().WithTargetCount(1),
		)
	}

	cs := &ControlService{
		StorageControlService: *NewStorageControlService(log,
			bdev.NewMockProvider(log, bmbc),
			scm.NewMockProvider(log, smbc, smsc),
			cfg.Engines,
		),
		harness: &EngineHarness{
			log: log,
		},
		events: events.NewPubSub(context.TODO(), log),
	}

	for _, srvCfg := range cfg.Engines {
		bp, err := bdev.NewClassProvider(log, "", &srvCfg.Storage.Bdev)
		if err != nil {
			t.Fatal(err)
		}
		runner := engine.NewTestRunner(&engine.TestRunnerConfig{
			Running: atm.NewBool(true),
		}, srvCfg)
		instance := NewEngineInstance(log, bp, cs.scm, nil, runner)
		instance.setSuperblock(&Superblock{
			Rank: system.NewRankPtr(srvCfg.Rank.Uint32()),
		})
		if err := cs.harness.AddInstance(instance); err != nil {
			t.Fatal(err)
		}
	}

	return cs
}

func mockControlServiceNoSB(t *testing.T, log logging.Logger, cfg *config.Server, bmbc *bdev.MockBackendConfig, smbc *scm.MockBackendConfig, smsc *scm.MockSysConfig) *ControlService {
	cs := mockControlService(t, log, cfg, bmbc, smbc, smsc)

	// don't set a superblock and init with a stopped test runner
	for i, srv := range cs.harness.instances {
		srv.setSuperblock(nil)
		srv.runner = engine.NewTestRunner(nil, cfg.Engines[i])
	}

	return cs
}
