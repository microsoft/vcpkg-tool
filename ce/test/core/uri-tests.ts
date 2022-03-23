// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { strictEqual } from 'assert';
import { SuiteLocal } from './SuiteLocal';

describe('Uri', () => {
  const local = new SuiteLocal();
  const fs = local.fs;

  after(local.after.bind(local));
  const tempUrl = local.tempFolderUri;
  const tempUrlForward = tempUrl.join().toString();

  it('Converts slashes on join', () => {
    const unixPath = fs.parse('/some/unixy/path').join();
    strictEqual(unixPath.toString(), 'file:///some/unixy/path');
    const windowsPath = fs.parse('C:\\Windows\\System32').join();
    strictEqual(windowsPath.toString(), 'C:/Windows/System32');
  });

  it('Can go to a child path', () => {
    const child = tempUrl.join('uriChild').toString();
    strictEqual(child, tempUrlForward + '/uriChild');
  });

  it('Can go to parent path', () => {
    const child = tempUrl.join('uriChild');
    const actual = child.parent.join().toString();
    strictEqual(actual.toString(), tempUrlForward);
  });
});
