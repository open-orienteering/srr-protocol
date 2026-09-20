# Legal considerations

This is a lay assessment written by the author, not legal advice. The
project is based in Sweden, so the EU/Swedish framework is what is
considered; a short note on other jurisdictions is at the end. If you plan
to build a product on this, talk to a lawyer.

## Summary

Reverse engineering the SRR air interface by observing a lawfully purchased
dongle, and publishing what was learned, is in the author's assessment
lawful in the EU:

* No software was copied, decompiled or modified. Only the *behaviour* of a
  device was observed — on a bus and over the air.
* The information obtained (frequencies, modulation, frame layout) is
  functional interface information, which copyright does not protect.
* Reverse engineering a product one lawfully possesses is explicitly a
  lawful way of acquiring information under EU trade-secret law.
* The signals are transmitted unencrypted in a licence-free band. Nothing
  was circumvented because there is nothing to circumvent.

The residual points to be aware of are radio regulations for the *transmit*
side (the ACK), possible patents on the AIR+/SRR *system* as opposed to the
protocol, and — most practically — being considerate about where an
ACKing receiver is switched on.

## Copyright and the Software Directive

Directive [2009/24/EC](https://eur-lex.europa.eu/eli/dir/2009/24/oj) (in
Sweden: upphovsrättslagen 26 g–h §§) governs computer programs.

* **Art. 1(2):** "Ideas and principles which underlie any element of a
  computer program, including those which underlie its interfaces, are not
  protected by copyright." The SRR frame format and register settings are
  exactly that kind of interface information.
* **Art. 5(3):** A person having a right to use a copy of a program may,
  without authorisation, "observe, study or test the functioning of the
  program in order to determine the ideas and principles which underlie any
  element of the program" while performing acts they are entitled to do
  (loading, running…). Plugging in the dongle and watching what it does is
  squarely within this.
* **Art. 6 (decompilation for interoperability)** has stricter conditions,
  but is not engaged: no code was reproduced or translated. The MSP430
  firmware was never read out.

Nothing in this repository is a copy of SPORTident code or documentation.
The Arduino sketch is an independent implementation.

## Trade secrets

Directive [2016/943](https://eur-lex.europa.eu/eli/dir/2016/943/oj) Art.
3(1)(b) (Sweden: lag (2018:558) om företagshemligheter, 3 §) says that
acquiring a trade secret by "observation, study, disassembly or testing of a
product or object that has been made available to the public or that is
lawfully in the possession of the acquirer of the information who is free
from any legally valid duty to limit the acquisition of the trade secret" is
**lawful**.

The dongle was bought normally. Hardware purchases do not come with a
click-through licence, and no non-disclosure or "no reverse engineering"
agreement was ever signed with SPORTident. SPORTident's Config+ software
has a licence, but it was not used for anything in this project.

## Radio regulation

**Receiving.** Listening in the 2.4 GHz ISM band requires no licence
anywhere. Swedish law (lagen om elektronisk kommunikation) does restrict
*disclosing* the content of radio messages one was not the intended
recipient of, but SRR punch data is broadcast precisely so that event
receivers can pick it up, and this project processes it in the same way and
for the same purpose as a dongle does. The protocol description itself
contains no message content.

**Transmitting.** The receiver transmits a short ACK. In the EU the CC2500
falls under the harmonised standard for wideband 2.4 GHz equipment (ETSI EN
300 328), with a limit of 100 mW EIRP (and 10 mW/MHz for non-adaptive
equipment). A CC2500 module transmits about 1 mW and is well inside that.
Whether a given breakout module is CE-marked as a complete radio module is
a separate question that matters if you *sell* a product, not for a hobby
receiver. In Sweden, low-power 2.4 GHz SRDs are licence-exempt under PTS's
exemption regulations.

**Do not jam.** During development an "interference generator" sketch was
used briefly, on a bench, to test channel hopping. Deliberately causing
interference is illegal in essentially every jurisdiction. That sketch is
not published, and you should not write your own.

## Patents

Copyright and trade-secret law do not stop you from *building* a receiver.
Patents might. SPORTident may hold patents on aspects of the AIR+/SIAC
system (for example the combination of 125 kHz wake-up and 2.4 GHz
data), and this has **not** been researched for this project. A search on
[Espacenet](https://worldwide.espacenet.com/) for applicant
"SPORTident" is the place to start before commercialising anything.
Documenting a protocol does not infringe a patent; making, selling or using
a device that practises a patented claim can.

## Trademarks

SPORTident, SIAC and AIR+ are trademarks of SPORTident GmbH. They are used
here to describe what this project interoperates with (nominative use).
This project is not affiliated with or endorsed by SPORTident.

## Personal data

A card number is a pseudonymous identifier that an organiser can link to a
person. Anyone operating a receiver at an event is processing personal data
in the same way they are when they use a dongle, and the same GDPR
obligations apply — normally covered by the organiser's existing
responsibilities. This repository contains no punch data from real events.

## Fair play and courtesy

A receiver that ACKs will stop the transmitter from retrying. That is
harmless when it is *your* receiver at *your* control. It is not harmless
if you switch on an ACKing receiver next to somebody else's official radio
control: if their receiver missed a frame and yours acknowledged it, the
retry that would have saved them never comes. Do not run an ACKing receiver
at an event you are not organising without agreement, and consider
`ENABLE_ACK false` when you only want to listen.

## Other jurisdictions (briefly)

* **United States.** Reverse engineering for interoperability is
  well-established fair use (*Sega v. Accolade*, *Sony v. Connectix*). The
  DMCA anti-circumvention provisions require a technological protection
  measure to be circumvented; there is none here. Transmitting falls under
  FCC Part 15 — the same "is the module certified" question as CE.
* **United Kingdom.** Retains the Software Directive's observe/study/test
  right (CDPA s. 50BA) and reverse engineering under trade-secret law.

## What would change this assessment

* Extracting or publishing SPORTident firmware.
* Bypassing an encryption or authentication scheme if SPORTident introduces
  one in future firmware.
* Selling receivers without checking patents and radio certification.
* Republishing SPORTident's own documentation or software.

None of these are part of this project.
