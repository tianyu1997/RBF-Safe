# Coordinated reservation agreements

RBF-Safe 4.6 assembles unanimous, replayable software evidence that every
listed deployment published the same continuous fleet-occupancy payload.
`CoordinatedReservationAgreement` is a coordination artifact. It remains
`Unknown`, never authorizes execution, and is not a distributed-consensus,
lease, scheduling, or controller protocol.

## Agreement contract

Include `<rbfsafe/modules/assurance.h>` and link `RBFSafe::coordination`.
`make_coordinated_reservation_agreement` accepts:

- a non-empty application-owned protocol ID;
- a positive round and, after round one, the exact parent agreement ID;
- a logical evaluation tick;
- one `ContinuousFleetOccupancyBundle`;
- one unique deployment ID for each independent
  `RotatingOccupancyPublicationHistory`; and
- explicit resource and cancellation limits.

The occupancy bundle must report
`CertifiedSeparatedUnderSweptEnvelopes`. This status is still
non-authorizing. Every history must contain a publication valid at the
evaluation tick whose exact payload length, SHA-256 digest, decoded occupancy
bundle, timeline, and workspace frame match every other participant.
Participants must have unique deployment IDs and unique
publisher-service/stream pairs.

The builder independently verifies each selected publication against the
embedded authorized service-trust prefix. The agreement records:

- the fleet occupancy and separation-report identities;
- the common payload digest and length;
- timeline, workspace frame, minimum separation, and common closed validity
  interval;
- each deployment's occupancy identity;
- publisher service, stream, key, sequence, and publication root/head;
- trust root/head and the exact historical trust bundle used to sign;
- the verified-publication identity; and
- the protocol, round, parent, and evaluation tick.

Participants are sorted canonically by deployment and publisher identity
before the agreement ID is computed. Input order therefore does not affect
the result.

## Replay and succession

`verify_coordinated_reservation_agreement` requires the original exact
occupancy bundle plus a caller-supplied deployment-to-history mapping. It
recomputes the agreement and compares every semantic field and identity.
Replay may use histories that have since advanced: the stored trust and
publication heads must still be valid prefixes, but do not have to be the
current heads.

The application must pin the agreement ID outside the agreement file's
rollback domain. Loading a checksummed file without comparing a retained ID
does not prove that it is the newest accepted agreement.

`verify_coordinated_reservation_successor` takes both agreements plus the
deployment-to-current-history mapping and requires:

- the same protocol and exactly the same participant membership;
- `successor.round == previous.round + 1`;
- an exact parent link to the previous agreement;
- a nondecreasing evaluation tick; and
- a strictly larger publisher sequence for every participant;
- the previous publication head to be a true prefix of the successor head in
  the supplied replayed history; and
- the previous trust head to be equal to or a true prefix of the successor
  trust head.

This rejects a higher-sequence statement on a fork. It is still deterministic
round succession, not leader election, quorum consensus, clock
synchronization, or a network commit.

## C++ sketch

```cpp
std::vector<std::string> deployments{"arm-a", "arm-b"};
std::vector<rbfsafe::RotatingOccupancyPublicationHistory> histories{
    arm_a_history, arm_b_history};

auto agreement = rbfsafe::make_coordinated_reservation_agreement(
    "cell-reservation-v1", 1, "", 42, occupancy, deployments, histories);
if (!agreement)
    return agreement.error();

auto replay = rbfsafe::verify_coordinated_reservation_agreement(
    agreement.value(), occupancy, deployments, histories);
if (!replay)
    return replay.error();

auto saved = agreement.value().save("reservation.json");
```

The complete deterministic example is
`examples/coordination/coordinated_reservation_quickstart.cpp`.

## Python sketch

```python
agreement = rbfsafe.make_coordinated_reservation_agreement(
    "cell-reservation-v1",
    1,
    "",
    42,
    occupancy,
    ["arm-a", "arm-b"],
    [arm_a_history, arm_b_history],
)
rbfsafe.verify_coordinated_reservation_agreement(
    agreement, occupancy, ["arm-a", "arm-b"], [arm_a_history, arm_b_history]
)
agreement.save("reservation.json")
```

Inspect only against a caller-retained ID:

```bash
rbfsafe-inspect reservation.json \
  --expected-reservation-agreement <agreement-id>

rbfsafe-inspect reservation.json <agreement-id>
```

The first command is the Python entry point and the second is the native
executable.

## Limits and failure modes

`CoordinatedReservationAgreementLoadOptions` bounds participant count and
payload bytes and carries a cancellation token. The payload-byte limit applies
both to the agreement file during load and to the common occupancy payload
during build/replay.

Construction and replay fail closed on invalid fields, ambiguous participant
mapping, duplicate identities, uncovered evaluation ticks, mismatched bytes,
timeline or frame mismatches, a non-separated fleet report, invalid trust or
publication prefixes, resource exhaustion, cancellation, or corruption.

## Safety boundary

An agreement proves only a deterministic relationship among supplied,
authenticated software artifacts. It does not prove:

- that a participant received or accepted the agreement;
- freshness relative to wall time or global newest-head status;
- clock synchronization, network delivery, availability, or Byzantine
  consensus;
- localization, calibration, obstacle perception, trajectory tracking, or
  dynamics;
- controller admission, reservation enforcement, mutual exclusion, emergency
  stopping, or physical execution; or
- collision freedom beyond the exact conservative models, trajectories,
  frames, uncertainty, and interval analysis recorded by the occupancy
  bundle.

Applications must combine caller-retained newest heads and agreement IDs,
trusted clock and transport policy, deployment-specific monitoring,
controller interlocks, and independent safety controls.

See [the schema-1 format](coordinated-reservation-agreement-format.md),
[continuous fleet occupancy](continuous-fleet-occupancy.md),
[trust-rotating histories](rotating-occupancy-publication-history.md), and
[the safety model](../../core/safety-model.md).
