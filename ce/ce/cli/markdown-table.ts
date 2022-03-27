// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { strict } from 'assert';

export class Table {
  private readonly rows = new Array<string>();
  private numberOfColumns = 0;
  constructor(...columnNames: Array<string>) {
    this.numberOfColumns = columnNames.length;
    this.rows.push(`|${columnNames.join('|')}|`);
    this.rows.push(`${'|--'.repeat(this.numberOfColumns)}|`);
  }
  push(...values: Array<string>) {
    strict.equal(values.length, this.numberOfColumns, 'unexpected number of arguments in table row');
    this.rows.push(`|${values.join('|')}|`);
  }
  toString() {
    return this.rows.join('\n');
  }
}