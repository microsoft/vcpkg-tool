const { unlinkSync, writeFileSync } = require('fs');
const { join } = require('path');

const file = join(__dirname, 'deploy-metadata.json');
const metadata = require(file);

const links = metadata.links;
metadata.links = links.filter(link => link.kind !== 'folderLink' || (link.linkPath.indexOf('/@types') === -1 && link.targetPath.indexOf('/typescript@') === -1));
writeFileSync(file, JSON.stringify(metadata, null, 2));
unlinkSync(__filename);