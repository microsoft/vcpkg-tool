// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { basename } from 'path';
import { FileType } from '../../fs/filesystem';
import { i } from '../../i18n';
import { session } from '../../main';
import { Uri } from '../../util/uri';
import { Command } from '../command';
import { Table } from '../console-table';
import { log } from '../styling';
import { Clear } from '../switches/clear';

export class CacheCommand extends Command {
  readonly command = 'cache';
  clear = new Clear(this);

  override async run() {
    if (this.clear.active) {
      await session.downloads.delete({ recursive: true });
      await session.downloads.createDirectory();
      log(i`Downloads folder cleared (${session.downloads.fsPath}) `);
      return true;
    }
    let files: Array<[Uri, FileType]> = [];
    try {
      files = await session.downloads.readDirectory();
    } catch {
      // shh
    }

    if (!files.length) {
      log('The download cache is empty');
      return true;
    }

    const table = new Table('File', 'Size', 'Date');
    for (const [file, ] of files) {
      const stat = await file.stat();
      table.push(basename(file.fsPath), stat.size.toString(), new Date(stat.mtime).toString());
    }
    log(table.toString());
    log();

    return true;
  }
}
