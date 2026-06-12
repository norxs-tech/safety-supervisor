#!/usr/bin/env python3
# =====================================================================================
# generate_sbom.py — SPDX 2.3 SBOM generator for the norxs safety-supervisor repo
#
# Usage:  python3 tools/generate_sbom.py [version]
# Output: sbom/safety-supervisor-<version>.spdx.json
#
# Regenerate the SBOM whenever files are added, removed, or modified, and commit
# the result. CI job `supply-chain-compliance` verifies that every repository
# file is catalogued (OpenChain ISO/IEC 5230 §3.3 / §3.4).
#
# (c) 2026 norxs Technology LLC. All rights reserved.
# =====================================================================================
import json
import hashlib
import os
import sys
import uuid
import datetime

VERSION = sys.argv[1] if len(sys.argv) > 1 else "0.9.1"
SKIP_DIRS = {"build", ".git", "sbom", ".github"}
SKIP_DIRS_KEEP_GITHUB = {"build", ".git", "sbom"}  # .github IS catalogued


def iter_files(root="."):
    out = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS_KEEP_GITHUB]
        for fn in sorted(filenames):
            rel = os.path.relpath(os.path.join(dirpath, fn), root)
            out.append(rel.replace(os.sep, "/"))
    return sorted(out)


def digest(path, algo):
    h = hashlib.new(algo)
    with open(path, "rb") as f:
        h.update(f.read())
    return h.hexdigest()


def main():
    files = iter_files()
    now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    file_entries, spdx_ids = [], []
    for i, rel in enumerate(files):
        sid = f"SPDXRef-File-{i:03d}"
        spdx_ids.append(sid)
        file_entries.append({
            "fileName": "./" + rel,
            "SPDXID": sid,
            "checksums": [
                {"algorithm": "SHA1", "checksumValue": digest(rel, "sha1")},
                {"algorithm": "SHA256", "checksumValue": digest(rel, "sha256")},
            ],
            "licenseConcluded": "LicenseRef-norxs-RI-1.0",
            "licenseInfoInFiles": ["LicenseRef-norxs-RI-1.0"],
            "copyrightText":
                "Copyright (c) 2026 norxs Technology LLC. All rights reserved.",
        })

    doc = {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": f"norxs-safety-supervisor-{VERSION}",
        "documentNamespace":
            f"https://www.norxs.com/spdx/safety-supervisor-{VERSION}-{uuid.uuid4()}",
        "creationInfo": {
            "created": now,
            "creators": [
                "Organization: norxs Technology LLC",
                "Tool: norxs-sbom-generator-1.0",
            ],
            "licenseListVersion": "3.23",
        },
        "documentDescribes": ["SPDXRef-Package-SafetySupervisor"],
        "packages": [{
            "name": "safety-supervisor",
            "SPDXID": "SPDXRef-Package-SafetySupervisor",
            "versionInfo": VERSION,
            "supplier": "Organization: norxs Technology LLC",
            "originator": "Organization: norxs Technology LLC",
            "downloadLocation": "https://github.com/norxs-tech/safety-supervisor",
            "homepage": "https://www.norxs.com/",
            "filesAnalyzed": True,
            "licenseConcluded": "LicenseRef-norxs-RI-1.0",
            "licenseDeclared": "LicenseRef-norxs-RI-1.0",
            "copyrightText":
                "Copyright (c) 2026 norxs Technology LLC. All rights reserved.",
            "description":
                "AUTOSAR R25-11 ASIL-D SEooC Safety-Supervisor Gateway reference "
                "implementation for NXP S32G Cortex-M7. "
                "Zero third-party runtime dependencies.",
            "primaryPackagePurpose": "FIRMWARE",
            "hasFiles": spdx_ids,
            "externalRefs": [{
                "referenceCategory": "OTHER",
                "referenceType": "purl",
                "referenceLocator":
                    f"pkg:github/norxs-tech/safety-supervisor@{VERSION}",
            }],
        }],
        "files": file_entries,
        "hasExtractedLicensingInfos": [{
            "licenseId": "LicenseRef-norxs-RI-1.0",
            "name": "norxs Reference Implementation License v1.0",
            "extractedText":
                "See LICENSE file in the repository root. Permits viewing, "
                "evaluation, citation with attribution, and contribution; "
                "commercial use requires a separate license agreement with "
                "norxs Technology LLC.",
            "seeAlsos": [
                "https://github.com/norxs-tech/safety-supervisor/blob/main/LICENSE"
            ],
        }],
        "relationships": [{
            "spdxElementId": "SPDXRef-DOCUMENT",
            "relatedSpdxElement": "SPDXRef-Package-SafetySupervisor",
            "relationshipType": "DESCRIBES",
        }],
    }

    os.makedirs("sbom", exist_ok=True)
    out = f"sbom/safety-supervisor-{VERSION}.spdx.json"
    with open(out, "w") as f:
        json.dump(doc, f, indent=2)
    print(f"SBOM written to {out}: {len(files)} files catalogued")


if __name__ == "__main__":
    main()
