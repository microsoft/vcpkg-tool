// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Session } from '../session';
import { Uri } from '../util/uri';
import { LocalFileSystem } from './local-filesystem';

export class VsixLocalFilesystem extends LocalFileSystem {
  private readonly vsixBaseUri: Uri | undefined;

  constructor(session: Session) {
    super(session);
    const programData = session.environment['ProgramData'];
    if (programData) {
      this.vsixBaseUri = this.file(programData).join('Microsoft/VisualStudio/Packages');
    }
  }

  /**
   * Creates a new URI from a string, e.g. `https://www.msft.com/some/path`,
   * `file:///usr/home`, or `scheme:with/path`.
   *
   * @param value A string which represents an URI (see `URI#toString`).
   */
  override parse(value: string, _strict?: boolean): Uri {
    return Uri.parseFilterVsix(this, value, _strict, this.vsixBaseUri);
  }
}
