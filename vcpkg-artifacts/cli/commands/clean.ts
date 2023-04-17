// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { deactivate } from '../../artifacts/activation';
import { i } from '../../i18n';
import { session } from '../../main';
import { Command } from '../command';
import { log } from '../styling';
import { Switch } from '../switch';

export class All extends Switch {
  switch = 'all';
  get help() {
    return [
      i`cleans out everything (cache, installed artifacts)`
    ];
  }
}

export class Downloads extends Switch {
  switch = 'downloads';
  get help() {
    return [
      i`cleans out the downloads cache`
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
  downloads = new Downloads(this);

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
      await deactivate(session, false);
      await session.installFolder.delete({ recursive: true });
      await session.installFolder.createDirectory();
      log(i`Installed Artifact folder cleared (${session.installFolder.fsPath}) `);
    }

    if (this.all.active || this.downloads.active) {
      await session.downloads.delete({ recursive: true });
      await session.downloads.createDirectory();
      log(i`Cache folder cleared (${session.downloads.fsPath}) `);
    }

    return true;
  }
}
