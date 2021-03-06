/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.dfs.uns;

public interface EntryOrBuilder extends
    // @@protoc_insertion_point(interface_extends:uns.Entry)
    com.google.protobuf.MessageOrBuilder {

  /**
   * <code>.uns.PropType type = 1;</code>
   *
   * @return The enum numeric value on the wire for type.
   */
  int getTypeValue();

  /**
   * <code>.uns.PropType type = 1;</code>
   *
   * @return The type.
   */
  io.daos.dfs.uns.PropType getType();

  /**
   * <code>uint32 reserved = 2;</code>
   *
   * @return The reserved.
   */
  int getReserved();

  /**
   * <code>uint64 val = 3;</code>
   *
   * @return The val.
   */
  long getVal();

  /**
   * <code>string str = 4;</code>
   *
   * @return The str.
   */
  java.lang.String getStr();

  /**
   * <code>string str = 4;</code>
   *
   * @return The bytes for str.
   */
  com.google.protobuf.ByteString
      getStrBytes();

  /**
   * <code>.uns.DaosAcl pval = 5;</code>
   *
   * @return Whether the pval field is set.
   */
  boolean hasPval();

  /**
   * <code>.uns.DaosAcl pval = 5;</code>
   *
   * @return The pval.
   */
  io.daos.dfs.uns.DaosAcl getPval();

  /**
   * <code>.uns.DaosAcl pval = 5;</code>
   */
  io.daos.dfs.uns.DaosAclOrBuilder getPvalOrBuilder();

  io.daos.dfs.uns.Entry.ValueCase getValueCase();
}
