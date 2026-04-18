import { useRef, useEffect } from 'react';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import './Orientation3D.css';

// Sensor slot angles in degrees (0 = North, clockwise).
// Slots 0-3 from config.h, slots 4-7 unmounted placeholders.
const SENSOR_ANGLES = [
  0,    // slot 0 = North
  180,  // slot 1 = South
  270,  // slot 2 = West
  90,   // slot 3 = East
  45,   // slot 4 = NE (unmounted)
  135,  // slot 5 = SE (unmounted)
  225,  // slot 6 = SW (unmounted)
  315,  // slot 7 = NW (unmounted)
];

export default function Orientation3D({ imu, sensors }) {
  const containerRef = useRef(null);
  const sceneRef = useRef(null);

  const yaw = imu?.yaw ?? 0;
  const pitch = imu?.pitch ?? 0;
  const roll = imu?.roll ?? 0;

  // Initialize Three.js scene once
  useEffect(() => {
    const container = containerRef.current;
    const width = container.clientWidth;
    const height = container.clientHeight;

    const renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setSize(width, height);
    renderer.setPixelRatio(window.devicePixelRatio);
    renderer.setClearColor(0x050505);
    container.appendChild(renderer.domElement);

    const scene = new THREE.Scene();

    const camera = new THREE.PerspectiveCamera(45, width / height, 0.1, 100);
    camera.position.set(3, 2, 3);
    camera.lookAt(0, 0, 0);

    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.1;
    controls.enablePan = false;
    controls.minDistance = 2;
    controls.maxDistance = 8;

    // Ground plane grid
    const grid = new THREE.GridHelper(6, 12, 0x222222, 0x111111);
    scene.add(grid);

    // Axes
    const axisLen = 2;
    function makeAxis(dir, color) {
      const mat = new THREE.LineBasicMaterial({ color });
      const geo = new THREE.BufferGeometry().setFromPoints([
        new THREE.Vector3(0, 0, 0),
        new THREE.Vector3(...dir).multiplyScalar(axisLen),
      ]);
      return new THREE.Line(geo, mat);
    }
    scene.add(makeAxis([1, 0, 0], 0xff0000)); // X = red
    scene.add(makeAxis([0, 1, 0], 0x00ff00)); // Y = green
    scene.add(makeAxis([0, 0, 1], 0x4444ff)); // Z = blue

    // Axis labels
    function makeLabel(text, position, color) {
      const canvas = document.createElement('canvas');
      canvas.width = 32;
      canvas.height = 32;
      const ctx = canvas.getContext('2d');
      ctx.fillStyle = color;
      ctx.font = 'bold 24px Courier New';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillText(text, 16, 16);
      const texture = new THREE.CanvasTexture(canvas);
      const sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map: texture }));
      sprite.position.copy(position);
      sprite.scale.set(0.3, 0.3, 1);
      return sprite;
    }
    scene.add(makeLabel('X', new THREE.Vector3(axisLen + 0.2, 0, 0), '#ff0000'));
    scene.add(makeLabel('Y', new THREE.Vector3(0, axisLen + 0.2, 0), '#00ff00'));
    scene.add(makeLabel('Z', new THREE.Vector3(0, 0, axisLen + 0.2), '#4444ff'));

    // Robot: flat cylinder with checkerboard + front marker
    const robotGroup = new THREE.Group();

    const checkerSize = 64;
    const checkerCanvas = document.createElement('canvas');
    checkerCanvas.width = checkerSize;
    checkerCanvas.height = checkerSize;
    const cctx = checkerCanvas.getContext('2d');
    const tileCount = 8;
    const tileSize = checkerSize / tileCount;
    for (let ty = 0; ty < tileCount; ty++) {
      for (let tx = 0; tx < tileCount; tx++) {
        cctx.fillStyle = (tx + ty) % 2 === 0 ? '#cccccc' : '#444444';
        cctx.fillRect(tx * tileSize, ty * tileSize, tileSize, tileSize);
      }
    }
    const checkerTex = new THREE.CanvasTexture(checkerCanvas);
    checkerTex.magFilter = THREE.NearestFilter;
    checkerTex.minFilter = THREE.NearestFilter;

    const bodyGeo = new THREE.CylinderGeometry(0.7, 0.7, 0.15, 32);
    const bodyMat = new THREE.MeshBasicMaterial({ map: checkerTex });
    const body = new THREE.Mesh(bodyGeo, bodyMat);
    body.position.y = 0;
    robotGroup.add(body);

    // Front arrow on top of disc
    const arrowShape = new THREE.Shape();
    arrowShape.moveTo(0, -0.5);
    arrowShape.lineTo(0.15, -0.1);
    arrowShape.lineTo(0.06, -0.1);
    arrowShape.lineTo(0.06, 0.3);
    arrowShape.lineTo(-0.06, 0.3);
    arrowShape.lineTo(-0.06, -0.1);
    arrowShape.lineTo(-0.15, -0.1);
    arrowShape.closePath();
    const arrowGeo = new THREE.ShapeGeometry(arrowShape);
    const arrowMat = new THREE.MeshBasicMaterial({ color: 0xff0000, side: THREE.DoubleSide });
    const arrow = new THREE.Mesh(arrowGeo, arrowMat);
    arrow.rotation.x = -Math.PI / 2;
    arrow.position.y = 0.08;
    robotGroup.add(arrow);

    // Sensor lines (8 directions, updated each frame)
    const sensorLines = [];
    const robotRadius = 0.7;
    const maxDist = 2000;
    const maxLineLen = 1.5;
    for (let i = 0; i < 8; i++) {
      const mat = new THREE.LineBasicMaterial({ color: 0x00ff00 });
      const geo = new THREE.BufferGeometry().setFromPoints([
        new THREE.Vector3(0, 0, 0),
        new THREE.Vector3(0, 0, 0),
      ]);
      const line = new THREE.Line(geo, mat);
      line.position.y = 0.08;
      robotGroup.add(line);
      sensorLines.push(line);
    }

    scene.add(robotGroup);

    sceneRef.current = { renderer, scene, camera, controls, robotGroup, sensorLines, robotRadius, maxDist, maxLineLen };

    const resizeObserver = new ResizeObserver(([entry]) => {
      const w = entry.contentRect.width;
      const h = entry.contentRect.height;
      if (w === 0 || h === 0) return;
      renderer.setSize(w, h);
      camera.aspect = w / h;
      camera.updateProjectionMatrix();
    });
    resizeObserver.observe(container);

    let animId;
    function animate() {
      animId = requestAnimationFrame(animate);
      controls.update();
      renderer.render(scene, camera);
    }
    animate();

    return () => {
      resizeObserver.disconnect();
      cancelAnimationFrame(animId);
      controls.dispose();
      renderer.dispose();
      container.removeChild(renderer.domElement);
    };
  }, []);

  // Update robot orientation
  useEffect(() => {
    if (!sceneRef.current) return;
    const { robotGroup } = sceneRef.current;
    const euler = new THREE.Euler(
      pitch * Math.PI / 180,
      -yaw * Math.PI / 180,
      roll * Math.PI / 180,
      'YXZ'
    );
    robotGroup.setRotationFromEuler(euler);
  }, [yaw, pitch, roll]);

  // Update sensor lines
  useEffect(() => {
    if (!sceneRef.current || !sensors) return;
    const { sensorLines, robotRadius, maxDist, maxLineLen } = sceneRef.current;
    for (let i = 0; i < 8; i++) {
      const angleDeg = SENSOR_ANGLES[i];
      const angleRad = angleDeg * Math.PI / 180;
      const dist = sensors[i] ?? -1;
      if (dist < 0) {
        const pos = sensorLines[i].geometry.attributes.position;
        pos.setXYZ(0, 0, 0, 0);
        pos.setXYZ(1, 0, 0, 0);
        pos.needsUpdate = true;
        sensorLines[i].material.color.setHex(0x333333);
        continue;
      }
      const lineLen = robotRadius + (dist / maxDist) * maxLineLen;

      // In Three.js XZ plane: angle 0 = forward (-Z)
      const sx = Math.sin(angleRad) * robotRadius;
      const sz = -Math.cos(angleRad) * robotRadius;
      const ex = Math.sin(angleRad) * lineLen;
      const ez = -Math.cos(angleRad) * lineLen;

      const positions = sensorLines[i].geometry.attributes.position;
      positions.setXYZ(0, sx, 0, sz);
      positions.setXYZ(1, ex, 0, ez);
      positions.needsUpdate = true;

      const color = dist < 150 ? 0xff0000 : dist < 400 ? 0xffff00 : 0x00ff00;
      sensorLines[i].material.color.setHex(color);
    }
  }, [sensors]);

  return (
    <div className="panel orientation-panel">
      <h2>3D ORIENTATION</h2>
      <div ref={containerRef} className="orientation-canvas" />
    </div>
  );
}
