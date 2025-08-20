// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { strict } from 'assert';
import chalk from 'chalk';
import stripAnsi from 'strip-ansi';

function leftPad(text: string, length: number) {
  const remain = length - text.length;
  if (remain <= 0) { return text; }
  return text + ' '.repeat(remain);
}

export class Table {
  private readonly columnNames: Array<string>;
  private readonly rows = new Array<Array<string>>();
  constructor(...columnNames: Array<string>) {
    this.columnNames = columnNames;
  }
  push(...values: Array<string>) {
    strict.equal(values.length, this.columnNames.length, 'unexpected number of arguments in table row');
    this.rows.push(Array.from(values));
  }
  toString() {
    const lengths = new Array<number>(this.columnNames.length);
    for (let colNum = 0; colNum < this.columnNames.length; ++colNum) {
      lengths[colNum] = this.columnNames[colNum].length;
    }

    for (const row of this.rows) {
      for (let colNum = 0; colNum < this.columnNames.length; ++colNum) {
        const colLen = stripAnsi(row[colNum]).length;
        if (colLen > lengths[colNum]) {
          lengths[colNum] = colLen;
        }
      }
    }

    const formattedRows = new Array<string>();
    const thisFormattedRow = new Array<string>(this.columnNames.length);
    for (let colNum = 0; colNum < this.columnNames.length; ++colNum) {
      thisFormattedRow[colNum] = chalk.red(leftPad(this.columnNames[colNum], lengths[colNum]));
    }

    formattedRows.push(thisFormattedRow.join(' '));

    for (const row of this.rows) {
      for (let colNum = 0; colNum < this.columnNames.length; ++colNum) {
        thisFormattedRow[colNum] = leftPad(row[colNum], lengths[colNum]);
      }

      formattedRows.push(thisFormattedRow.join(' '));
    }

    return formattedRows.join('\n');
  }
}
