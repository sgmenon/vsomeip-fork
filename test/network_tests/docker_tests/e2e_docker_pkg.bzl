# Copyright (C) 2026 GM GLOBAL TECHNOLOGY OPERATIONS LLC ALL RIGHTS RESERVED.
"""pkg_tar packages for Bazel+Docker network suites.

Each suite tar extracts into PKG_DIR with:
  app/<bins>  app/lib/<vsomeip *.so*>  app/run-*.sh
  configs/*.json  docker_infra/
"""

load("@rules_pkg//pkg:mappings.bzl", "pkg_attributes", "pkg_files", "strip_prefix")
load("@rules_pkg//pkg:tar.bzl", "pkg_tar")

def e2e_docker_pkg(name, client, service, config_srcs):
    """Build a self-contained staging tar for one E2E Docker suite.

    Args:
      name: suite directory name (e.g. "e2e_crc"); creates `:<name>_pkg`.
      client: cc_binary label for the client.
      service: cc_binary label for the service.
      config_srcs: JSON config files staged under configs/.
    """
    bins_name = name + "_bins"
    configs_name = name + "_configs"

    pkg_files(
        name = bins_name,
        testonly = True,
        srcs = [client, service],
        attributes = pkg_attributes(mode = "0755"),
        prefix = "app",
    )

    pkg_files(
        name = configs_name,
        testonly = True,
        srcs = config_srcs,
        prefix = "configs",
        strip_prefix = strip_prefix.from_pkg(name + "/configs"),
    )

    pkg_tar(
        name = name + "_pkg",
        testonly = True,
        srcs = [
            ":" + bins_name,
            ":" + configs_name,
            ":vsomeip_runtime_libs",
            ":e2e_common_scripts",
            ":docker_infra_files",
        ],
        extension = "tar",
        visibility = ["//visibility:public"],
    )
