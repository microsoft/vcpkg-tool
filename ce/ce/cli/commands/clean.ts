// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { session } from '../../main';
import { Command } from '../command';
import { debug, log } from '../styling';
import { Switch } from '../switch';
import { WhatIf } from '../switches/whatIf';

export class All extends Switch {
  switch = 'all';
  get help() {
    return [
      i`cleans out everything (cache, installed artifacts)`
    ];
  }
}

export class Cache extends Switch {
  switch = 'cache';
  get help() {
    return [
      i`cleans out the cache`
    ];
  }
}

export class Artifacts extends Switch {
  switch = 'artifacts';
  get help() {
    return [
      i`removes all the artifacts that are installed`
    ];
  }
}

export class CleanCommand extends Command {
  readonly command = 'clean';
  readonly aliases = [];
  seeAlso = [];
  argumentsHelp = [];
  all = new All(this);
  artifacts = new Artifacts(this);
  cache = new Cache(this);
  whatIf = new WhatIf(this);

  get summary() {
    return i`cleans up`;
  }

  get description() {
    return [
      i`Allows the user to clean out the cache, installed artifacts, etc.`,
    ];
  }

  override async run() {

    if (this.all.active || this.artifacts.active) {
      // if we're removing artifacts
      debug(i`Deactivating project`);
      await session.deactivate();

      await session.installFolder.delete({ recursive: true });
      await session.installFolder.createDirectory();
      log(i`Installed Artifact folder cleared (${session.installFolder.fsPath}) `);
    }

    if (this.all.active || this.cache.active) {
      await session.cache.delete({ recursive: true });
      await session.cache.createDirectory();
      log(i`Cache folder cleared (${session.cache.fsPath}) `);
    }

    return true;
  }
}