import { RTCPeerConnection, RTCSessionDescription, RTCIceCandidate, RTCDataChannel, RTCStatsResponse, RTCOfferOptions } from 'wrtc';
import * as dgram from 'dgram';
import options from './options';
import logger from './logger';
import { EventEmitter } from 'events';

export class PeerRelay extends EventEmitter {
  peerConnection: RTCPeerConnection;
  localSocket: dgram.Socket;
  localPort: number;
  dataChannel: RTCDataChannel;
  iceConnectionState: string;
  iceGatheringState: string;
  startTime: [number, number];
  connectedTime: [number, number];
  loc_cand_addr: string;
  rem_cand_addr: string;
  loc_cand_type: string;
  rem_cand_type: string;
  iceServers: Array<any>;
  constructor(public remoteId: number, public remoteLogin: string, public createOffer: boolean, public twilioToken?: any) {
    super();
    this.startTime = process.hrtime();
    this.iceConnectionState = 'None';
    this.loc_cand_addr = 'none';
    this.rem_cand_addr = 'none';
    this.loc_cand_type = 'none';
    this.rem_cand_type = 'none';
    this.iceServers = [
      {
        urls: [`turn:test.faforever.com?transport=tcp`],
        credential: 'test',
        username: 'test'
      },
      {
        urls: [`turn:test.faforever.com?transport=udp`],
        credential: 'test',
        username: 'test'
      },
      {
        urls: [`stun:test.faforever.com`],
        credential: '',
        username: ''
      }];

    if (this.twilioToken) {
      this.iceServers = this.iceServers.concat(this.twilioToken['ice_servers']);
    }
    else {
      logger.warn("missing twilio token");
    }

    this.initPeerConnection();
    this.initLocalSocket();

    logger.info(`Relay for ${remoteLogin}(${remoteId}): successfully created`);
  }

  initPeerConnection() {
    /* https://webrtc.github.io/samples/src/content/peerconnection/trickle-ice/ 
       TURN servers require credentials with webrtc! */

    if (!this.twilioToken) {
      logger.error("!this.twilioToken");
    }
    logger.debug(`Relay for ${this.remoteLogin}(${this.remoteId}): iceServers: ${JSON.stringify(this.iceServers)}`);
    this.peerConnection = new RTCPeerConnection({
      iceServers: this.iceServers
    });

    this.peerConnection.onerror = (event) => {
      this.handleError("peerConnection.onerror", event);
    };

    this.peerConnection.ondatachannel = (event) => {
      this.initDataChannel(event.channel);
    };

    this.peerConnection.onicecandidate = (candidate) => {
      if (candidate.candidate) {
        logger.debug(`PeerRelay for ${this.remoteLogin} received candidate ${JSON.stringify(candidate.candidate)}`);
        this.emit('iceMessage', {
          'type': 'candidate',
          'candidate': candidate.candidate
        });
      }
    }

    this.peerConnection.oniceconnectionstatechange = (event) => {
      this.iceConnectionState = this.peerConnection.iceConnectionState;
      logger.debug(`Relay for ${this.remoteLogin}(${this.remoteId}): iceConnectionState changed to ${this.iceConnectionState}`);
      if ((this.iceConnectionState == 'connected' || this.iceConnectionState == 'completed')
        && !this.connectedTime) {
        this.connectedTime = process.hrtime(this.startTime);
        logger.info(`Relay for ${this.remoteLogin}(${this.remoteId}): connection established after ${this.connectedTime[1] / 1e9}s`);

        this.peerConnection.getStats((stats) => {
          stats.result().forEach((stat) => {
            if (stat.type == 'googCandidatePair' &&
              stat.stat('googActiveConnection') === 'true') {
              this.loc_cand_addr = stat.stat('googLocalAddress');
              this.rem_cand_addr = stat.stat('googRemoteAddress');
              this.loc_cand_type = stat.stat('googLocalCandidateType');
              this.rem_cand_type = stat.stat('googRemoteCandidateType');
            }
          });
        },
          (error) => {
            this.handleError("getStats failed", error);
          });

      }

      if (this.iceConnectionState == 'failed') {
        this.tryReconnect();
      }

      this.emit('iceConnectionStateChanged', this.iceConnectionState);
    };

    this.peerConnection.onicegatheringstatechange = (event) => {
      this.iceGatheringState = this.peerConnection.iceGatheringState;
      logger.debug(`Relay for ${this.remoteLogin}(${this.remoteId}): iceGatheringState changed to ${this.iceGatheringState}`);
    };

    if (this.createOffer) {
      logger.info(`Relay for ${this.remoteLogin}(${this.remoteId}): create offer`);

      this.initDataChannel(this.peerConnection.createDataChannel('faf', {
        ordered: false,
        maxRetransmits: 0,
      }));

      this.peerConnection.createOffer().then((desc: RTCSessionDescription) => {
        this.peerConnection.setLocalDescription(new RTCSessionDescription(desc)).then(() => {
          this.emit('iceMessage', desc);
        },
          (error) => {
            this.handleError("setLocalDescription with offer failed", error);
          }
        );
      }, (error) => {
        this.handleError("createOffer failed", error);
      });
    }

  }

  initDataChannel(dc: RTCDataChannel) {
    if (this.dataChannel) {
      this.dataChannel.close();
      delete this.dataChannel;
    }
    dc.onopen = () => {
      this.dataChannel = dc;
      logger.info(`Relay for ${this.remoteLogin}(${this.remoteId}): data channel open`);
      this.dataChannel.onmessage = (event) => {
        if (this.localSocket) {
          this.localSocket.send(Buffer.from(event.data), options.lobbyPort, 'localhost', (error, bytes) => {
            if (error) {
              logger.error(`Relay for ${this.remoteLogin}(${this.remoteId}): error sending to local socket: ${JSON.stringify(error)}`);
            }
          });
        }
      }
      this.emit('datachannelOpen');
      if (!this.connectedTime) {
        this.connectedTime = process.hrtime(this.startTime);
        logger.info(`Relay for ${this.remoteLogin}(${this.remoteId}): connection established after ${this.connectedTime[1] / 1e9}s`);
      }
    };
    dc.onclose = () => {
      logger.info(`Relay for ${this.remoteLogin}(${this.remoteId}): data channel close`);
      if (this.dataChannel) {
        delete this.dataChannel;
      }
    }
  }

  initLocalSocket() {
    this.localSocket = dgram.createSocket('udp4');

    this.localSocket.bind(undefined, 'localhost', () => {
      this.localPort = this.localSocket.address().port;
      logger.info(`Relay for ${this.remoteLogin}(${this.remoteId}): listening on port ${this.localPort}`);
      this.emit('localSocketListening');
    });

    this.localSocket.on('message', (msg, rinfo) => {
      //logger.info(`Relay for ${this.remoteLogin}(${this.remoteId}): received msg ${msg} from ${JSON.stringify(rinfo)}}`);
      if (this.dataChannel) {
        this.dataChannel.send(msg);
      }
    });

    this.localSocket.on('error', (error) => {
      logger.info(`Relay for ${this.remoteLogin}(${this.remoteId}): error in localsocket: ${JSON.stringify(error)}`);
    });
    this.localSocket.on('close', () => {
      logger.info(`Relay for ${this.remoteLogin}(${this.remoteId}): local socket closed`);
      delete this.localSocket;
    });
  }

  addIceMsg(msg: any) {
    logger.debug(`Relay for ${this.remoteLogin}(${this.remoteId}): received ICE msg: ${JSON.stringify(msg)}`);
    if (msg.type == 'offer') {
      this.peerConnection.setRemoteDescription(new RTCSessionDescription(msg)).then(() => {
        this.peerConnection.createAnswer().then((desc: RTCSessionDescription) => {
          this.peerConnection.setLocalDescription(new RTCSessionDescription(desc)).then(() => {
            this.emit('iceMessage', desc);
          },
            (error) => {
              this.handleError("setLocalDescription with answer failed", error);
            }
          );
        },
          (error) => {
            this.handleError("createAnswer() for offer failed", error);
          }
        );
      },
        (error) => {
          this.handleError("setRemoteDescription failed", error);
        }
      );
    }
    else if (msg.type == 'answer') {
      this.peerConnection.setRemoteDescription(new RTCSessionDescription(msg)).then(() => {
        logger.debug(`Relay for ${this.remoteLogin}(${this.remoteId}): set remote answer`);
      },
        (error) => {
          this.handleError("setRemoteDescription failed", error);
        });
    }
    else if (msg.type == 'candidate') {
      this.peerConnection.addIceCandidate(new RTCIceCandidate(msg.candidate)).then(() => {
        logger.debug(`Relay for ${this.remoteLogin}(${this.remoteId}): added ICE candidate ${JSON.stringify(msg.candidate)}`);
      },
        (error) => {
          this.handleError("addIceCandidate failed", error);
        });
    }
    else {
      logger.error(`Relay for ${this.remoteLogin}(${this.remoteId}): unknown ICE message type: ${JSON.stringify(msg)}`);
    }
  }

  handleError(what: string, error) {
    logger.error(`Relay for ${this.remoteLogin}(${this.remoteId}) ${what}: ${JSON.stringify(error)}`);
  }

  close() {
    this.peerConnection.close();
    this.localSocket.close();
  }

  tryReconnect() {
    if (this.iceConnectionState == 'failed' || this.iceConnectionState == 'new') {
      logger.error(`Relay for ${this.remoteLogin}(${this.remoteId}): connection failed. Trying reconnect...`);

      this.peerConnection.close();
      delete this.peerConnection;
      this.initPeerConnection();
      setTimeout(() => {
        this.tryReconnect();
      }, 5000);
    }
  }
}
