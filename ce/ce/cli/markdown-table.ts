// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { strict } from 'assert';
import { blue, gray, green, white } from 'chalk';
import * as renderer from 'marked-terminal';


// eslint-disable-next-line @typescript-eslint/no-var-requires
const marked = require('marked');

// setup markdown renderer
marked.setOptions({
  renderer: new renderer({
    tab: 2,
    emoji: true,
    showSectionPrefix: false,
    firstHeading: green.underline.bold,
    heading: green.underline,
    codespan: white.bold,
    link: blue.bold,
    href: blue.bold.underline,
    code: gray,
    tableOptions: {
      chars: {
        'top': '', 'top-mid': '', 'top-left': '', 'top-right': ''
        , 'bottom': '', 'bottom-mid': '', 'bottom-left': '', 'bottom-right': ''
        , 'left': '', 'left-mid': '', 'mid': '', 'mid-mid': ''
        , 'right': '', 'right-mid': '', 'middle': ''
      }
    }
  }),
  gfm: true,
});

export class Table {
  private readonly rows = new Array<string>();
  private numberOfColumns = 0;
  public anyRows = false;
  constructor(...columnNames: Array<string>) {
    this.numberOfColumns = columnNames.length;
    this.rows.push(`|${columnNames.join('|')}|`);
    this.rows.push(`${'|--'.repeat(this.numberOfColumns)}|`);
  }
  push(...values: Array<string>) {
    strict.equal(values.length, this.numberOfColumns, 'unexpected number of arguments in table row');
    this.rows.push(`|${values.join('|')}|`);
    this.anyRows = true;
  }
  toString() {
    return marked.marked(this.rows.join('\n'));
  }
}