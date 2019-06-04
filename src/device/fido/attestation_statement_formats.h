// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_FIDO_ATTESTATION_STATEMENT_FORMATS_H_
#define DEVICE_FIDO_ATTESTATION_STATEMENT_FORMATS_H_

#include <stdint.h>
#include <memory>
#include <vector>

#include "base/component_export.h"
#include "base/containers/span.h"
#include "base/macros.h"
#include "components/cbor/values.h"
#include "device/fido/attestation_statement.h"
#include "device/fido/fido_constants.h"

namespace device {

// https://www.w3.org/TR/2017/WD-webauthn-20170505/#fido-u2f-attestation
class COMPONENT_EXPORT(DEVICE_FIDO) FidoAttestationStatement
    : public AttestationStatement {
 public:
  static std::unique_ptr<FidoAttestationStatement>
  CreateFromU2fRegisterResponse(base::span<const uint8_t> u2f_data);

  FidoAttestationStatement(std::vector<uint8_t> signature,
                           std::vector<std::vector<uint8_t>> x509_certificates);
  ~FidoAttestationStatement() override;

  // AttestationStatement
  cbor::Value::MapValue GetAsCBORMap() const override;
  bool IsSelfAttestation() override;
  bool IsAttestationCertificateInappropriatelyIdentifying() override;
  base::Optional<base::span<const uint8_t>> GetLeafCertificate() const override;

 private:
  const std::vector<uint8_t> signature_;
  const std::vector<std::vector<uint8_t>> x509_certificates_;

  DISALLOW_COPY_AND_ASSIGN(FidoAttestationStatement);
};

// Implements the "packed" attestation statement format from
// https://www.w3.org/TR/webauthn/#packed-attestation.
//
// It currently only supports the (optional) "x5c" field, but not "ecdaaKeyId"
// (see packedStmtFormat choices).
class COMPONENT_EXPORT(DEVICE_FIDO) PackedAttestationStatement
    : public AttestationStatement {
 public:
  PackedAttestationStatement(
      CoseAlgorithmIdentifier algorithm,
      std::vector<uint8_t> signature,
      std::vector<std::vector<uint8_t>> x509_certificates);
  ~PackedAttestationStatement() override;

  // AttestationStatement
  cbor::Value::MapValue GetAsCBORMap() const override;
  bool IsSelfAttestation() override;
  bool IsAttestationCertificateInappropriatelyIdentifying() override;
  base::Optional<base::span<const uint8_t>> GetLeafCertificate() const override;

 private:
  const CoseAlgorithmIdentifier algorithm_;
  const std::vector<uint8_t> signature_;
  const std::vector<std::vector<uint8_t>> x509_certificates_;
};

}  // namespace device

#endif  // DEVICE_FIDO_ATTESTATION_STATEMENT_FORMATS_H_
