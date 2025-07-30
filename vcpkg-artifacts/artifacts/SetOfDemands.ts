// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { MetadataFile } from '../amf/metadata-file';
import { Demands } from '../interfaces/metadata/demands';
import { Installer } from '../interfaces/metadata/installers/Installer';
import { VersionReference } from '../interfaces/metadata/version-reference';
import { parseQuery } from '../mediaquery/media-query';
import { Session } from '../session';
import { MultipleInstallsMatched } from '../util/exceptions';
import { linq } from '../util/linq';


export class SetOfDemands {
  _demands = new Map<string, Demands>();

  constructor(metadata: MetadataFile, session: Session) {
    this._demands.set('', metadata);

    for (const [query, demands] of metadata.conditionalDemands) {
      if (parseQuery(query).match(session.context)) {
        session.channels.debug(`Matching demand query: '${query}'`);
        this._demands.set(query, demands);
      }
    }
  }

  get installer(): Iterable<Installer> {
    const install = linq.entries(this._demands).where(([, demand]) => demand.install.length > 0).toArray();

    if (install.length > 1) {
      // bad. There should only ever be one install block.
      throw new MultipleInstallsMatched(install.map(each => each[0]));
    }

    return install[0]?.[1].install || [];
  }

  get errors() {
    return linq.values(this._demands).selectNonNullable(d => d.error).toArray();
  }
  get warnings() {
    return linq.values(this._demands).selectNonNullable(d => d.warning).toArray();
  }
  get messages() {
    return linq.values(this._demands).selectNonNullable(d => d.message).toArray();
  }
  get exports() {
    return linq.values(this._demands).selectNonNullable(d => d.exports).toArray();
  }

  get requires() {
    const d = this._demands;
    const rq1 = linq.values(d).selectNonNullable(d => d.requires).toArray();
    const result : Record<string, VersionReference> = {};
    for (const dict of rq1) {
      for (const [query, demands] of dict) {
        result[query] = demands;
      }
    }
    const rq = [...d.values()].map(each => each.requires).filter(each => each);

    for (const dict of rq) {
      for (const [query, demands] of dict) {
        result[query] = demands;
      }
    }
    return result;
  }
}
