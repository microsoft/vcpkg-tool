// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { basename } from 'path';
import { FileType } from '../../fs/filesystem';
import { i } from '../../i18n';
import { session } from '../../main';
import { Uri } from '../../util/uri';
import { Command } from '../command';
import { Table } from '../markdown-table';
import { log } from '../styling';
import { Clear } from '../switches/clear';
import { WhatIf } from '../switches/whatIf';

export class CacheCommand extends Command {
  readonly command = 'cache';
  readonly aliases = [];
  seeAlso = [];
  argumentsHelp = [];
  clear = new Clear(this);
  whatIf = new WhatIf(this);

  get summary() {
    return i`Manages the download cache`;
  }

  get description() {
    return [
      i`Manages the download cache.`,
    ];
  }

  override async run() {
    if (this.clear.active) {
      await session.cache.delete({ recursive: true });
      await session.cache.createDirectory();
      log(i`Cache folder cleared (${session.cache.fsPath}) `);
      return true;
    }
    let files: Array<[Uri, FileType]> = [];
    try {
      files = await session.cache.readDirectory();
    } catch {
      // shh
    }

    if (!files.length) {
      log('The download cache is empty');
      return true;
    }

    const table = new Table('File', 'Size', 'Date');
    for (const [file, type] of files) {
      const stat = await file.stat();
      table.push(basename(file.fsPath), stat.size.toString(), new Date(stat.mtime).toString());
    }
    log(table.toString());
    log();

    return true;
  }
}